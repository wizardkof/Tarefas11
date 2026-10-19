#include "PeIconExtractor.h"

#include <QBuffer>
#include <QFile>
#include <QImageReader>
#include <QVector>
#include <QtEndian>

#include <algorithm>
#include <cstdint>
#include <limits>

namespace {
constexpr quint64 kMaxResource = 32ULL * 1024 * 1024;
constexpr quint64 kMaxIcon = 8ULL * 1024 * 1024;

quint16 u16(const QByteArray &data, qsizetype off)
{
    if (off < 0 || off > data.size() - 2) return 0;
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(data.constData() + off));
}
quint32 u32(const QByteArray &data, qsizetype off)
{
    if (off < 0 || off > data.size() - 4) return 0;
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + off));
}
void put16(QByteArray &dest, quint16 value)
{
    dest.append(static_cast<char>(value & 0xff));
    dest.append(static_cast<char>((value >> 8) & 0xff));
}
void put32(QByteArray &dest, quint32 value)
{
    put16(dest, static_cast<quint16>(value & 0xffff));
    put16(dest, static_cast<quint16>(value >> 16));
}
QByteArray block(QFile &file, quint64 position, quint64 size)
{
    if (size == 0 || size > kMaxResource || position > static_cast<quint64>(file.size()) ||
        size > static_cast<quint64>(file.size()) - position ||
        !file.seek(static_cast<qint64>(position))) return {};
    return file.read(static_cast<qint64>(size));
}

struct Section { quint32 virtualAddress = 0, virtualSize = 0, rawAddress = 0, rawSize = 0; };

struct Resources {
    QFile &file;
    QVector<Section> sections;
    quint32 rootRva = 0;
    QByteArray bytes;

    quint64 fileOffset(quint32 rva, quint32 size) const
    {
        for (const auto &s : sections) {
            const quint64 delta = quint64(rva) >= s.virtualAddress ? quint64(rva) - s.virtualAddress : UINT64_MAX;
            if (delta > s.rawSize || quint64(size) > quint64(s.rawSize) - delta) continue;
            const quint64 off = quint64(s.rawAddress) + delta;
            if (off <= quint64(file.size()) && size <= quint64(file.size()) - off) return off;
        }
        return UINT64_MAX;
    }

    // Retorna posição na tabela de recursos (offset relativo a rootRva) para ID.
    quint32 child(int directory, int id) const
    {
        if (directory < 0 || directory > bytes.size() - 16) return UINT32_MAX;
        const int count = int(u16(bytes, directory + 12)) + int(u16(bytes, directory + 14));
        if (count <= 0 || count > 4096 || quint64(directory) + 16ULL + quint64(count) * 8 > quint64(bytes.size())) return UINT32_MAX;
        for (int i = 0; i < count; ++i) {
            const int pos = directory + 16 + i * 8;
            const quint32 key = u32(bytes, pos);
            if (!(key & 0x80000000U) && int(key & 0xffff) == id)
                return u32(bytes, pos + 4);
        }
        return UINT32_MAX;
    }
    quint32 first(int directory) const
    {
        if (directory < 0 || directory > bytes.size() - 24) return UINT32_MAX;
        if (int(u16(bytes, directory + 12)) + int(u16(bytes, directory + 14)) < 1) return UINT32_MAX;
        return u32(bytes, directory + 20);
    }
    int treeNode(quint32 value) const
    {
        if (!(value & 0x80000000U)) return -1;
        const quint32 at = value & 0x7fffffffU;
        return at <= quint32(bytes.size()) ? int(at) : -1;
    }
    QByteArray data(int resourceType, int resourceId = -1)
    {
        int node = treeNode(child(0, resourceType));
        if (node < 0) return {};
        const quint32 nameEntry = resourceId >= 0 ? child(node, resourceId) : first(node);
        node = treeNode(nameEntry);
        if (node < 0) return {};
        const quint32 leaf = first(node); // language => leaf data entry
        if (leaf == UINT32_MAX || (leaf & 0x80000000U) || leaf > quint32(bytes.size() - 16)) return {};
        node = int(leaf);
        const quint32 rva = u32(bytes, node);
        const quint32 size = u32(bytes, node + 4);
        if (!size || size > kMaxIcon) return {};
        const quint64 at = fileOffset(rva, size);
        return at == UINT64_MAX ? QByteArray{} : block(file, at, size);
    }
};
}

QImage PeIconExtractor::extract(const QString &exePath, int requestedSize)
{
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 512) return {};
    const auto dos = block(file, 0, 64);
    if (dos.size() != 64 || dos.left(2) != "MZ") return {};
    const quint32 peOffset = u32(dos, 0x3c);
    if (peOffset > quint64(file.size()) || quint64(peOffset) + 24 > quint64(file.size())) return {};
    const auto header = block(file, peOffset, 24);
    if (header.left(4) != QByteArray("PE\0\0", 4)) return {};
    const int sectionCount = u16(header, 6);
    const int optionalSize = u16(header, 20);
    if (sectionCount < 1 || sectionCount > 96 || optionalSize < 104 || optionalSize > 4096) return {};
    const auto optional = block(file, quint64(peOffset) + 24, optionalSize);
    const int directoryOffset = u16(optional, 0) == 0x20b ? 112 :
                                u16(optional, 0) == 0x10b ? 96 : -1;
    if (directoryOffset < 0 || optional.size() < directoryOffset + 24) return {};
    const quint32 rva = u32(optional, directoryOffset + 16); // IMAGE_DIRECTORY_ENTRY_RESOURCE
    const quint32 resourceSize = u32(optional, directoryOffset + 20);
    if (!rva || !resourceSize || resourceSize > kMaxResource) return {};

    const auto sectionBytes = block(file, quint64(peOffset) + 24 + optionalSize, sectionCount * 40ULL);
    if (sectionBytes.size() != sectionCount * 40) return {};
    Resources res{file, {}, rva, {}};
    for (int i = 0; i < sectionCount; ++i) {
        const int off = i * 40;
        res.sections.append({u32(sectionBytes, off + 12), u32(sectionBytes, off + 8),
                             u32(sectionBytes, off + 20), u32(sectionBytes, off + 16)});
    }
    const auto resourceOffset = res.fileOffset(rva, resourceSize);
    if (resourceOffset == UINT64_MAX) return {};
    res.bytes = block(file, resourceOffset, resourceSize);
    if (res.bytes.size() < 24) return {};

    const auto group = res.data(14); // RT_GROUP_ICON
    if (group.size() < 20 || u16(group, 0) != 0 || u16(group, 2) != 1) return {};
    const int count = std::min<int>(u16(group, 4), 256);
    if (count == 0 || quint64(count) * 14 + 6 > quint64(group.size())) return {};

    // Escolhe a resolução mais próxima, preferindo imagens >= ao tamanho solicitado.
    QVector<int> indices;
    indices.reserve(count);
    for (int i = 0; i < count; ++i) indices.append(i);
    std::sort(indices.begin(), indices.end(), [&](int a, int b) {
        const auto score = [&](int index) {
            const int at = 6 + index * 14;
            const int w = static_cast<uchar>(group[at]) == 0 ? 256 : static_cast<uchar>(group[at]);
            const int h = static_cast<uchar>(group[at + 1]) == 0 ? 256 : static_cast<uchar>(group[at + 1]);
            const int side = std::max(w, h);
            return side >= requestedSize ? side - requestedSize : (requestedSize - side) * 3;
        };
        return score(a) < score(b);
    });

    for (int index : indices) {
        const int at = 6 + index * 14;
        const quint16 iconId = u16(group, at + 12);
        const QByteArray payload = res.data(3, iconId); // RT_ICON
        if (payload.isEmpty()) continue;
        QByteArray ico;
        ico.reserve(22 + payload.size());
        put16(ico, 0); put16(ico, 1); put16(ico, 1);
        ico.append(group.constData() + at, 4); // width, height, colors, reserved
        put16(ico, u16(group, at + 4));
        put16(ico, u16(group, at + 6));
        put32(ico, static_cast<quint32>(payload.size()));
        put32(ico, 22);
        ico.append(payload);
        QBuffer buffer(&ico);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "ico");
        reader.setAutoTransform(true);
        QImage result = reader.read();
        if (result.isNull() && payload.startsWith("\x89PNG\r\n\x1a\n"))
            result = QImage::fromData(payload, "PNG");
        if (!result.isNull()) return result;
    }
    return {};
}
