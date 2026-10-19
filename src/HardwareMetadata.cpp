#include "HardwareMetadata.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace {
QString readText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.read(4096)).trimmed() : QString{};
}

QString commonOrMixed(const QSet<QString> &values)
{
    if (values.isEmpty()) return {};
    QStringList sorted;
    for (const auto &value : values) sorted.append(value);
    sorted.sort(Qt::CaseInsensitive);
    return sorted.join(QStringLiteral(" / "));
}

quint64 hardwareId(QString id)
{
    id = id.trimmed();
    bool ok = false;
    const quint64 value = id.toULongLong(&ok, 0);
    if (ok) return value;
    // PCI IDs em sysfs geralmente têm prefixo 0x; evitar correspondência por texto parcial.
    return ~quint64(0);
}
}

namespace HardwareMetadata {

MemoryInfo parseMemoryUdev(const QByteArray &properties)
{
    MemoryInfo summary;
    QHash<int, QHash<QString, QString>> devices;
    const QRegularExpression prop(QStringLiteral("^MEMORY_DEVICE_([0-9]+)_(.+)$"));
    for (const QByteArray &raw : properties.split('\n')) {
        const qsizetype at = raw.indexOf('=');
        if (at <= 0) continue;
        const QString key = QString::fromUtf8(raw.left(at)).trimmed();
        const QString value = QString::fromUtf8(raw.mid(at + 1)).trimmed();
        if (key == QStringLiteral("MEMORY_ARRAY_NUM_DEVICES")) {
            summary.slotCount = value.toInt();
            continue;
        }
        const auto match = prop.match(key);
        if (match.hasMatch()) devices[match.captured(1).toInt()].insert(match.captured(2), value);
    }

    quint64 capacity = 0;
    QSet<QString> types, speeds, manufacturers;
    for (auto it = devices.cbegin(); it != devices.cend(); ++it) {
        const auto &d = it.value();
        bool ok = false;
        const quint64 bytes = d.value(QStringLiteral("SIZE")).toULongLong(&ok);
        if (!ok || bytes == 0 || d.value(QStringLiteral("PRESENT")) == QStringLiteral("0")) continue;
        capacity += bytes;
        ++summary.modules;
        const QString type = d.value(QStringLiteral("TYPE"));
        if (!type.isEmpty() && type != QStringLiteral("Unknown")) types.insert(type);
        const QString speed = d.value(QStringLiteral("CONFIGURED_SPEED_MTS"));
        if (!speed.isEmpty() && speed != QStringLiteral("0") && speed != QStringLiteral("Unknown"))
            speeds.insert(speed);
        const QString maker = d.value(QStringLiteral("MANUFACTURER"));
        if (!maker.isEmpty() && maker != QStringLiteral("Unknown")) manufacturers.insert(maker);
    }
    if (summary.modules > 0) summary.installedGiB = double(capacity) / 1073741824.0;
    summary.type = commonOrMixed(types);
    const QString speed = commonOrMixed(speeds);
    if (!speed.isEmpty()) summary.configuredSpeed = speed + QStringLiteral(" MT/s (DMI)");
    summary.slotsText = summary.slotCount > 0 ? QStringLiteral("%1 de %2").arg(summary.modules).arg(summary.slotCount)
                                              : QString::number(summary.modules) + QStringLiteral(" ocupados");
    if (summary.modules > 0) {
        summary.modulesText = QStringLiteral("%1 módulo(s)").arg(summary.modules);
        if (!manufacturers.isEmpty()) summary.modulesText += QStringLiteral(" · ") + commonOrMixed(manufacturers);
    }
    return summary;
}

RamTemperatures ramTemperatures(const QString &hwmonPath)
{
    RamTemperatures result;
    QStringList readings;
    const QDir sys(hwmonPath);
    const QStringList nodes = sys.entryList({QStringLiteral("hwmon*")}, QDir::Dirs | QDir::System | QDir::NoDotAndDotDot);
    for (const QString &node : nodes) {
        const QString path = sys.filePath(node + QLatin1Char('/'));
        const QString driver = readText(path + QStringLiteral("name")).toLower();
        // JEDEC SPD5118 (DDR5) e JC42 (DIMM DDR3/4). 'amdgpu', 'k10temp', etc. não são sensores da RAM.
        if (driver != QStringLiteral("spd5118") && driver != QStringLiteral("jc42")) continue;
        bool ok = false;
        const double milliCelsius = readText(path + QStringLiteral("temp1_input")).toDouble(&ok);
        if (!ok || milliCelsius < -40000 || milliCelsius > 125000) continue;
        readings << QString::number(milliCelsius / 1000.0, 'f', 1) + QStringLiteral(" °C");
    }
    result.sensorCount = readings.size();
    result.display = readings.isEmpty() ? QStringLiteral("Indisponível (sensor SPD)")
                                         : readings.join(QStringLiteral(" · "));
    return result;
}

QVector<VulkanGpu> parseVulkanSummary(const QByteArray &output)
{
    QVector<VulkanGpu> result;
    VulkanGpu gpu;
    bool insideGpu = false;
    const QRegularExpression heading(QStringLiteral("^GPU[0-9]+\\s*:$"));
    auto finish = [&]() {
        if (!gpu.apiVersion.isEmpty() && !gpu.vendorId.isEmpty() && !gpu.deviceId.isEmpty())
            result.append(gpu);
        gpu = {};
    };
    for (const QByteArray &raw : output.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (heading.match(line).hasMatch()) {
            if (insideGpu) finish();
            insideGpu = true;
            continue;
        }
        if (!insideGpu) continue;
        const int pos = line.indexOf('=');
        if (pos < 0) continue;
        const QString field = line.left(pos).trimmed();
        const QString value = line.mid(pos + 1).trimmed();
        if (field == QStringLiteral("apiVersion")) gpu.apiVersion = value;
        else if (field == QStringLiteral("vendorID")) gpu.vendorId = value;
        else if (field == QStringLiteral("deviceID")) gpu.deviceId = value;
        else if (field == QStringLiteral("deviceName")) gpu.deviceName = value;
        else if (field == QStringLiteral("driverName")) gpu.driverName = value;
        else if (field == QStringLiteral("deviceType")) gpu.deviceType = value;
    }
    if (insideGpu) finish();
    return result;
}

bool vulkanMatchesDevice(const VulkanGpu &candidate, const QString &sysVendor, const QString &sysDevice)
{
    if (candidate.deviceType.contains(QStringLiteral("CPU"), Qt::CaseInsensitive) ||
        candidate.deviceName.contains(QStringLiteral("llvmpipe"), Qt::CaseInsensitive)) return false;
    const quint64 vendor = hardwareId(sysVendor);
    const quint64 device = hardwareId(sysDevice);
    return vendor != ~quint64(0) && device != ~quint64(0) &&
           hardwareId(candidate.vendorId) == vendor && hardwareId(candidate.deviceId) == device;
}

} // namespace HardwareMetadata
