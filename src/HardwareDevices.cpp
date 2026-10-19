#include "SystemMonitor.h"
#include "HardwareMetadata.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QProcess>
#include <QTimer>
#include <QSet>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace {
QByteArray fileBytes(const QString &name) {
    QFile f(name);
    return f.open(QIODevice::ReadOnly) ? f.readAll().trimmed() : QByteArray{};
}
QString fileText(const QString &name) { return QString::fromUtf8(fileBytes(name)); }
quint64 u64(const QString &name) { return fileBytes(name).toULongLong(); }
double gibs(quint64 bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0); }
QString pciId(QString raw) {
    raw = raw.trimmed().toLower();
    const auto match = QRegularExpression(QStringLiteral("(?:[0-9a-f]{4,8}:)?([0-9a-f]{2}:[0-9a-f]{2}\\.[0-7])$")).match(raw);
    return match.hasMatch() ? match.captured(1) : raw;
}
QString fieldFrom(const QByteArray &data, const QByteArray &name) {
    for (const auto &line : data.split('\n'))
        if (line.startsWith(name + '=')) return QString::fromUtf8(line.mid(name.size() + 1));
    return {};
}
double mbRate(quint64 current, quint64 previous, double sec) {
    return current >= previous && sec > 0 ? static_cast<double>(current - previous) / (1048576.0 * sec) : 0;
}
QString deviceVendor(const QString &vendor) {
    if (vendor == QStringLiteral("0x1002")) return QStringLiteral("AMD Radeon");
    if (vendor == QStringLiteral("0x10de")) return QStringLiteral("NVIDIA GeForce");
    if (vendor == QStringLiteral("0x8086")) return QStringLiteral("Intel Graphics");
    return QStringLiteral("GPU PCI ") + vendor;
}
// Identificação offline pelo banco de IDs PCI do sistema. Drivers como amdgpu
// expõem o ID de dispositivo, mas nem sempre o nome comercial da iGPU.
QString pciModel(const QString &vendor, const QString &device)
{
    const QString vendorId = vendor.trimmed().mid(2).toLower();
    const QString deviceId = device.trimmed().mid(2).toLower();
    if (vendorId.size() != 4 || deviceId.size() != 4) return {};
    const QStringList databases = {
        QStringLiteral("/usr/share/hwdata/pci.ids"),
        QStringLiteral("/usr/share/misc/pci.ids"),
        QStringLiteral("/usr/share/pci.ids")
    };
    for (const QString &database : databases) {
        QFile file(database);
        if (!file.open(QIODevice::ReadOnly)) continue;
        bool inVendor = false;
        while (!file.atEnd()) {
            const QByteArray raw = file.readLine();
            if (raw.startsWith('#') || raw.trimmed().isEmpty()) continue;
            if (!raw.startsWith('\t')) {
                if (raw.size() >= 6 && raw[4] == ' ') {
                    inVendor = QString::fromLatin1(raw.left(4)).compare(vendorId, Qt::CaseInsensitive) == 0;
                } else inVendor = false;
            } else if (inVendor && raw.size() > 7 && raw[1] != '\t' && raw[5] == ' ' &&
                       QString::fromLatin1(raw.mid(1, 4)).compare(deviceId, Qt::CaseInsensitive) == 0) {
                return QString::fromUtf8(raw.mid(7)).trimmed();
            }
        }
    }
    return {};
}
}

QVariantList SystemMonitor::devices() const {
    QVariantList result;
    for (const QString &kind : {QStringLiteral("disk"), QStringLiteral("net"), QStringLiteral("gpu")}) {
        int gpuNumber = 0;
        for (auto it = m_deviceInfo.cbegin(); it != m_deviceInfo.cend(); ++it) {
            if (it.value().value(QStringLiteral("kind")).toString() != kind) continue;
            QVariantMap row = it.value();
            if (kind == QStringLiteral("gpu")) row.insert(QStringLiteral("label"), QStringLiteral("GPU %1").arg(gpuNumber++));
            result.append(row);
        }
    }
    return result;
}
QVariantMap SystemMonitor::deviceInfo(const QString &key) const {
    QVariantMap out = m_deviceInfo.value(key);
    if (out.value(QStringLiteral("kind")) == QStringLiteral("gpu")) {
        int gpuNumber = 0;
        for (auto it = m_deviceInfo.cbegin(); it != m_deviceInfo.cend(); ++it) {
            if (it.value().value(QStringLiteral("kind")) != QStringLiteral("gpu")) continue;
            if (it.key() == key) { out.insert(QStringLiteral("label"), QStringLiteral("GPU %1").arg(gpuNumber)); break; }
            ++gpuNumber;
        }
    }
    return out;
}
QVariantList SystemMonitor::historyFor(const QString &key) const { return list(m_deviceHistories.value(key)); }

void SystemMonitor::upsertDevice(const QString &key, const QVariantMap &attributes) {
    auto &target = m_deviceInfo[key];
    target.insert(QStringLiteral("key"), key);
    for (auto it = attributes.cbegin(); it != attributes.cend(); ++it) target.insert(it.key(), it.value());
}

void SystemMonitor::sampleDisks(double seconds) {
    static const QRegularExpression physical(QStringLiteral("^(?:nvme[0-9]+n[0-9]+|sd[a-z]+|vd[a-z]+|xvd[a-z]+|mmcblk[0-9]+)$"));
    const QStringList dirs = QDir(QStringLiteral("/sys/block")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    double totalRead = 0, totalWrite = 0, busyTop = 0;
    for (const QString &dev : dirs) {
        if (!physical.match(dev).hasMatch()) continue;
        const QString base = QStringLiteral("/sys/block/") + dev;
        const auto stats = fileBytes(base + QStringLiteral("/stat")).simplified().split(' ');
        if (stats.size() < 10) continue;
        const quint64 readBytes = stats[2].toULongLong() * 512ULL;
        const quint64 writeBytes = stats[6].toULongLong() * 512ULL;
        const quint64 busyMillis = stats[9].toULongLong();
        const quint64 readIos = stats[0].toULongLong();
        const quint64 writeIos = stats[4].toULongLong();
        const quint64 ioMillis = stats[3].toULongLong() + stats[7].toULongLong();
        const bool baseline = m_diskLastRead.contains(dev) && m_diskLastWrite.contains(dev) && m_diskLastBusy.contains(dev);
        const double read = baseline ? mbRate(readBytes, m_diskLastRead.value(dev), seconds) : 0.0;
        const double write = baseline ? mbRate(writeBytes, m_diskLastWrite.value(dev), seconds) : 0.0;
        const double busy = baseline && busyMillis >= m_diskLastBusy.value(dev)
            ? std::clamp((busyMillis - m_diskLastBusy.value(dev)) / (seconds * 10.0), 0.0, 100.0) : 0.0;
        const bool haveLatency = baseline && m_diskLastReadIos.contains(dev) &&
            m_diskLastWriteIos.contains(dev) && m_diskLastIoMillis.contains(dev) &&
            readIos >= m_diskLastReadIos.value(dev) && writeIos >= m_diskLastWriteIos.value(dev) &&
            ioMillis >= m_diskLastIoMillis.value(dev);
        const quint64 completed = haveLatency ?
            (readIos - m_diskLastReadIos.value(dev)) + (writeIos - m_diskLastWriteIos.value(dev)) : 0;
        // /sys/block/DEVICE/stat: ticks de leitura + escrita / E/S concluídas no período.
        // Não é medida de latência por aplicação, nem inclui discard/flush.
        const double responseMs = completed ? double(ioMillis - m_diskLastIoMillis.value(dev)) / completed : -1.0;
        const double readIops = haveLatency ? (readIos - m_diskLastReadIos.value(dev)) / seconds : 0.0;
        const double writeIops = haveLatency ? (writeIos - m_diskLastWriteIos.value(dev)) / seconds : 0.0;
        m_diskLastReadIos[dev] = readIos;
        m_diskLastWriteIos[dev] = writeIos;
        m_diskLastIoMillis[dev] = ioMillis;
        m_diskLastRead[dev] = readBytes;
        m_diskLastWrite[dev] = writeBytes;
        m_diskLastBusy[dev] = busyMillis;
        totalRead += read;
        totalWrite += write;
        busyTop = std::max(busyTop, busy);
        const QString id = QStringLiteral("disk:") + dev;
        QString model = fileText(base + QStringLiteral("/device/model")).simplified();
        if (model.isEmpty()) model = fileText(base + QStringLiteral("/device/name")).simplified();
        if (model.isEmpty()) model = fileText(base + QStringLiteral("/device/type")).simplified();
        if (model.isEmpty()) model = dev;
        const quint64 sizeBytes = u64(base + QStringLiteral("/size")) * 512ULL;
        const QString bus = dev.startsWith(QStringLiteral("nvme")) ? QStringLiteral("NVMe") :
                dev.startsWith(QStringLiteral("mmc")) ? QStringLiteral("eMMC/SD") :
                dev.startsWith(QStringLiteral("vd")) ? QStringLiteral("VirtIO") : QStringLiteral("SATA/SCSI/USB");
        const QString media = fileText(base + QStringLiteral("/queue/rotational")) == QStringLiteral("0")
            ? QStringLiteral("SSD / flash") : QStringLiteral("Rotacional");
        upsertDevice(id, {{"kind", "disk"}, {"label", QStringLiteral("Disco ") + dev},
            {"model", model}, {"subtitle", QStringLiteral("%1 · %2% · %3 MiB/s").arg(bus).arg(busy, 0, 'f', 0).arg(read + write, 0, 'f', 1)},
            {"percent", busy}, {"readMiBs", read}, {"writeMiBs", write},
            {"responseMs", responseMs}, {"readIops", readIops}, {"writeIops", writeIops},
            {"capacityGiB", gibs(sizeBytes)}, {"media", media}, {"bus", bus},
            {"device", QStringLiteral("/dev/") + dev},
            {"active", true}, {"percentInfo", "Tempo em que houve operações de E/S (/sys/block/*/stat), não saturação de banda"}});
        appendHistory(m_deviceHistories[id], busy);
    }
    m_diskReadMBs = totalRead;
    m_diskWriteMBs = totalWrite;
    m_diskBusyPercent = busyTop; // Maior tempo ativo dentre os dispositivos; não somamos percentuais.
}

void SystemMonitor::sampleNetworks(double seconds) {
    const auto nics = QNetworkInterface::allInterfaces();
    double totalRx = 0, totalTx = 0;
    for (const auto &iface : nics) {
        const QString name = iface.name();
        if ((iface.flags() & QNetworkInterface::IsLoopBack) || name.startsWith(QStringLiteral("veth"))) continue;
        const QString base = QStringLiteral("/sys/class/net/") + name;
        if (!QFileInfo::exists(base)) continue;
        const quint64 rx = u64(base + QStringLiteral("/statistics/rx_bytes"));
        const quint64 tx = u64(base + QStringLiteral("/statistics/tx_bytes"));
        const bool valid = m_netLastRx.contains(name) && m_netLastTx.contains(name);
        const double rxMB = valid ? mbRate(rx, m_netLastRx.value(name), seconds) : 0;
        const double txMB = valid ? mbRate(tx, m_netLastTx.value(name), seconds) : 0;
        m_netLastRx[name] = rx;
        m_netLastTx[name] = tx;
        const bool synthetic = name.startsWith(QStringLiteral("docker")) || name.startsWith(QStringLiteral("virbr")) ||
                name.startsWith(QStringLiteral("br-")) || name.startsWith(QStringLiteral("podman"));
        if (!synthetic) { totalRx += rxMB; totalTx += txMB; }
        QString ipv4, ipv6;
        for (const auto &address : iface.addressEntries()) {
            const QHostAddress ip = address.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol) {
                if (!ipv4.isEmpty()) ipv4 += QStringLiteral(", ");
                ipv4 += ip.toString();
            } else if (ip.protocol() == QAbstractSocket::IPv6Protocol && !ip.isLinkLocal()) {
                if (!ipv6.isEmpty()) ipv6 += QStringLiteral(", ");
                ipv6 += ip.toString();
            }
        }
        QString gateway;
        // /proc/net/route: nome, destino, gateway IPv4 little-endian, flags etc.
        const auto routes = fileBytes(QStringLiteral("/proc/net/route")).split('\n');
        for (const auto &line : routes) {
            const auto fields = line.trimmed().split('\t');
            if (fields.size() < 3 || QString::fromUtf8(fields[0]) != name || fields[1] != "00000000") continue;
            bool ok = false;
            const quint32 gatewayLe = fields[2].toUInt(&ok, 16);
            if (ok) gateway = QStringLiteral("%1.%2.%3.%4")
                .arg(gatewayLe & 255).arg((gatewayLe >> 8) & 255).arg((gatewayLe >> 16) & 255).arg((gatewayLe >> 24) & 255);
            break;
        }
        const QString speedText = fileText(base + QStringLiteral("/speed"));
        bool speedOk = false;
        const int linkSpeed = speedText.toInt(&speedOk);
        const QString carrierText = fileText(base + QStringLiteral("/operstate"));
        const QString type = name.startsWith(QStringLiteral("wl")) ? QStringLiteral("Wi-Fi") :
                 name.startsWith(QStringLiteral("tun")) || name.startsWith(QStringLiteral("wg")) ? QStringLiteral("VPN / túnel") :
                 synthetic ? QStringLiteral("Rede virtual") : QStringLiteral("Ethernet");
        const QString id = QStringLiteral("net:") + name;
        const double linkPercent = speedOk && linkSpeed > 0
            ? std::clamp(std::max(rxMB, txMB) * 1048576.0 * 8.0 / (double(linkSpeed) * 1000000.0) * 100.0, 0.0, 100.0)
            : -1.0;
        upsertDevice(id, {{"kind", "net"}, {"label", type + QStringLiteral(" ") + name},
            {"model", name}, {"subtitle", QStringLiteral("↓ %1 · ↑ %2 MiB/s").arg(rxMB, 0, 'f', 2).arg(txMB, 0, 'f', 2)},
            {"percent", linkPercent}, {"readMiBs", rxMB}, {"writeMiBs", txMB},
            {"connectionName", m_connections.value(name, QStringLiteral("—"))},
            {"ipv4", ipv4.isEmpty() ? QStringLiteral("—") : ipv4},
            {"ipv6", ipv6.isEmpty() ? QStringLiteral("—") : ipv6},
            {"mac", iface.hardwareAddress().isEmpty() ? QStringLiteral("—") : iface.hardwareAddress()},
            {"gateway", gateway.isEmpty() ? QStringLiteral("—") : gateway},
            {"mtu", iface.maximumTransmissionUnit()},
            {"linkSpeed", speedOk && linkSpeed > 0 ? QString::number(linkSpeed) + QStringLiteral(" Mbps") : QStringLiteral("—")},
            {"state", carrierText.isEmpty() ? QStringLiteral("—") : carrierText},
            {"active", bool(iface.flags() & QNetworkInterface::IsUp)},
            {"percentInfo", "Uso do link calculado por maior direção (RX ou TX), só quando velocidade negociada está disponível"}});
        appendHistory(m_deviceHistories[id], rxMB + txMB);
    }
    m_netRxMBs = totalRx;
    m_netTxMBs = totalTx;
}

void SystemMonitor::enumerateGpus() {
    const QStringList cards = QDir(QStringLiteral("/sys/class/drm")).entryList({QStringLiteral("card[0-9]*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    QSet<QString> known;
    for (const QString &card : cards) {
        const QString root = QStringLiteral("/sys/class/drm/") + card + QStringLiteral("/device/");
        const QString vendor = fileText(root + QStringLiteral("vendor"));
        if (vendor.isEmpty()) continue;
        const auto uevent = fileBytes(root + QStringLiteral("uevent"));
        const QString pci = fieldFrom(uevent, "PCI_SLOT_NAME");
        const QString driverName = fieldFrom(uevent, "DRIVER");
        const QString deviceId = fileText(root + QStringLiteral("device"));
        QString deviceName = fileText(root + QStringLiteral("product_name")).simplified();
        if (deviceName.isEmpty()) deviceName = pciModel(vendor, deviceId);
        QString displayName = deviceVendor(vendor);
        if (!deviceName.isEmpty()) {
            // Famílias de APUs (Raphael, Phoenix…) são nomes de silício quando
            // o driver não expõe o modelo comercial. Não inventar "Radeon 780M".
            displayName += QStringLiteral(" — ") + deviceName;
        } else {
            displayName += QStringLiteral(" · PCI ") + deviceId;
        }
        const QString key = QStringLiteral("gpu:") + (pci.isEmpty() ? card : pci);
        if (known.contains(key)) continue;
        known.insert(key);
        if (!pci.isEmpty()) m_gpuPciToKey.insert(pciId(pci), key);
        upsertDevice(key, {{"kind", "gpu"}, {"label", QStringLiteral("GPU ") + QString::number(known.size()-1)},
            {"model", displayName},
            {"subtitle", QStringLiteral("%1 · %2").arg(card, pci)},
            {"pci", pci}, {"vendor", vendor}, {"deviceId", deviceId}, {"drmCard", card}, {"driver", driverName},
            {"percent", -1.0}, {"memoryUsedMiB", -1.0}, {"memoryTotalMiB", -1.0},
            {"encodePercent", -1.0}, {"decodePercent", -1.0},
            {"renderPercent", -1.0}, {"copyPercent", -1.0},
            {"temperature", -1}, {"graphicsApi", "Indisponível (consulta por GPU não implementada)"},
            {"active", true}});
    }
}

void SystemMonitor::sampleGpus() {
    m_drmEngines.update();
    // GPU AMD/Intel: dados disponíveis sem privilégios nos drivers DRM/AMDGPU.
    for (auto it = m_deviceInfo.begin(); it != m_deviceInfo.end(); ++it) {
        if (it.value().value(QStringLiteral("kind")).toString() != QStringLiteral("gpu")) continue;
        const QString pci = it.value().value(QStringLiteral("pci")).toString().toLower();
        const auto engines = m_drmEngines.device(pci);
        const bool nvidia = it.value().value(QStringLiteral("driver")).toString().contains(QStringLiteral("nvidia"),Qt::CaseInsensitive);
        if (engines.render >= 0) {
            it.value().insert(QStringLiteral("renderPercent"), engines.render);
            appendHistory(m_deviceHistories[it.key() + QStringLiteral("#renderPercent")], engines.render);
        }
        if (engines.copy >= 0) {
            it.value().insert(QStringLiteral("copyPercent"), engines.copy);
            appendHistory(m_deviceHistories[it.key() + QStringLiteral("#copyPercent")], engines.copy);
        }
        if (engines.encode >= 0 && !nvidia) {
            it.value().insert(QStringLiteral("encodePercent"), engines.encode);
            appendHistory(m_deviceHistories[it.key() + QStringLiteral("#encodePercent")], engines.encode);
        }
        if (engines.decode >= 0 && !nvidia) {
            it.value().insert(QStringLiteral("decodePercent"), engines.decode);
            appendHistory(m_deviceHistories[it.key() + QStringLiteral("#decodePercent")], engines.decode);
        }
        if (engines.render >= 0 || engines.copy >= 0 || engines.encode >= 0 || engines.decode >= 0)
            it.value().insert(QStringLiteral("engineSource"), QStringLiteral("DRM fdinfo (clientes acessíveis; estimativa)"));
        const QString card = it.value().value(QStringLiteral("drmCard")).toString();
        if (card.isEmpty()) continue;
        const QString root = QStringLiteral("/sys/class/drm/") + card + QStringLiteral("/device/");
        const QString gpuBusy = fileText(root + QStringLiteral("gpu_busy_percent"));
        const QString vramTotal = fileText(root + QStringLiteral("mem_info_vram_total"));
        const QString vramUsed = fileText(root + QStringLiteral("mem_info_vram_used"));
        if (!gpuBusy.isEmpty()) {
            const double utilization = std::clamp(gpuBusy.toDouble(), 0.0, 100.0);
            it.value().insert(QStringLiteral("percent"), utilization);
            it.value().insert(QStringLiteral("subtitle"), QStringLiteral("%1% · %2").arg(utilization, 0, 'f', 0).arg(card));
            appendHistory(m_deviceHistories[it.key()], utilization);
        }
        if (!vramTotal.isEmpty()) it.value().insert(QStringLiteral("memoryTotalMiB"), vramTotal.toDouble() / 1048576.0);
        if (!vramUsed.isEmpty()) it.value().insert(QStringLiteral("memoryUsedMiB"), vramUsed.toDouble() / 1048576.0);
        // Sensores hwmon anexos a este dispositivo PCI, evitando ler a CPU como GPU.
        const QDir sensors(root + QStringLiteral("hwmon"));
        for (const QString &node : sensors.entryList({QStringLiteral("hwmon*")}, QDir::Dirs | QDir::System | QDir::NoDotAndDotDot)) {
            bool valid = false;
            const double millidegrees = fileText(sensors.filePath(node + QStringLiteral("/temp1_input"))).toDouble(&valid);
            if (valid && millidegrees > -40000 && millidegrees < 125000) {
                it.value().insert(QStringLiteral("temperature"), millidegrees / 1000.0);
                break;
            }
        }
        const auto linkSpeed = fileText(root + QStringLiteral("current_link_speed"));
        const auto linkWidth = fileText(root + QStringLiteral("current_link_width"));
        if (!linkSpeed.isEmpty() || !linkWidth.isEmpty())
            it.value().insert(QStringLiteral("pcieLink"), QStringLiteral("%1 × %2").arg(linkSpeed, linkWidth));
    }
}

void SystemMonitor::pollGraphicsApi() {
    // Consulta Vulkan por dispositivo físico. OpenGL mostra contexto gráfico padrão.
    const QString vk = QStandardPaths::findExecutable(QStringLiteral("vulkaninfo"));
    if (!vk.isEmpty() && m_apiProcess.state() == QProcess::NotRunning) {
        connect(&m_apiProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                if (code != 0 || status != QProcess::NormalExit) return;
                const auto physical = HardwareMetadata::parseVulkanSummary(m_apiProcess.readAllStandardOutput());
                QStringList inventory;
                QHash<QString, QStringList> versions, driverNames, deviceNames;
                for (const auto &gpu : physical) {
                    inventory << QStringLiteral("%1: %2").arg(gpu.deviceName, gpu.apiVersion);
                    QStringList matching;
                    for (auto it = m_deviceInfo.cbegin(); it != m_deviceInfo.cend(); ++it) {
                        if (it.value().value(QStringLiteral("kind")).toString() != QStringLiteral("gpu")) continue;
                        if (HardwareMetadata::vulkanMatchesDevice(gpu,
                                it.value().value(QStringLiteral("vendor")).toString(),
                                it.value().value(QStringLiteral("deviceId")).toString()))
                            matching << it.key();
                    }
                    // PCI vendor + device identifica um modelo; se existir mais de uma placa
                    // idêntica, não há como decidir qual é qual sem PCI bus-info: não inventar.
                    if (matching.size() != 1) continue;
                    const QString key = matching.first();
                    if (!versions[key].contains(gpu.apiVersion)) versions[key].append(gpu.apiVersion);
                    if (!gpu.driverName.isEmpty() && !driverNames[key].contains(gpu.driverName))
                        driverNames[key].append(gpu.driverName);
                    if (!gpu.deviceName.isEmpty() && !deviceNames[key].contains(gpu.deviceName))
                        deviceNames[key].append(gpu.deviceName);
                }
                for (auto it = m_deviceInfo.begin(); it != m_deviceInfo.end(); ++it) {
                    if (it.value().value(QStringLiteral("kind")).toString() != QStringLiteral("gpu")) continue;
                    if (versions.contains(it.key())) {
                        it.value().insert(QStringLiteral("vulkanVersion"), versions.value(it.key()).join(QStringLiteral(" / ")));
                        it.value().insert(QStringLiteral("vulkanDriver"), driverNames.value(it.key()).join(QStringLiteral(" / ")));
                        it.value().insert(QStringLiteral("vulkanDeviceName"), deviceNames.value(it.key()).join(QStringLiteral(" / ")));
                    } else {
                        it.value().insert(QStringLiteral("vulkanVersion"), QStringLiteral("— (não associado a esta GPU)"));
                        it.value().remove(QStringLiteral("vulkanDriver"));
                        it.value().remove(QStringLiteral("vulkanDeviceName"));
                    }
                }
                m_graphicsApiInfo = inventory.isEmpty() ? QStringLiteral("Nenhuma GPU Vulkan identificada")
                                                      : inventory.join(QStringLiteral(" | "));
                emit updated();
            }, Qt::SingleShotConnection);
        m_apiProcess.start(vk, {QStringLiteral("--summary")});
        QTimer::singleShot(3000, this, [this]() {
            if (m_apiProcess.state() != QProcess::NotRunning) m_apiProcess.kill();
        });
    }
    const QString gl = QStandardPaths::findExecutable(QStringLiteral("glxinfo"));
    if (!gl.isEmpty() && m_glProcess.state() == QProcess::NotRunning) {
        connect(&m_glProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                if (code != 0 || status != QProcess::NormalExit) return;
                QString renderer, version;
                for (const auto &line : QString::fromUtf8(m_glProcess.readAllStandardOutput()).split('\n')) {
                    if (line.startsWith(QStringLiteral("OpenGL renderer string:"))) renderer = line.section(':', 1).trimmed();
                    if (line.startsWith(QStringLiteral("OpenGL core profile version string:"))) version = line.section(':', 1).trimmed();
                    else if (version.isEmpty() && line.startsWith(QStringLiteral("OpenGL version string:"))) version = line.section(':', 1).trimmed();
                }
                if (!version.isEmpty()) {
                    m_openGlInfo = QStringLiteral("%1 — %2 (contexto OpenGL padrão)").arg(version, renderer);
                    emit updated();
                }
            }, Qt::SingleShotConnection);
        m_glProcess.start(gl, {QStringLiteral("-B")});
        QTimer::singleShot(3000, this, [this]() { if (m_glProcess.state() != QProcess::NotRunning) m_glProcess.kill(); });
    }
}

void SystemMonitor::requestMemoryHardwareDetails() {
    if (m_dmiProcess.state() != QProcess::NotRunning) return;
    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    const QString dmi = QStandardPaths::findExecutable(QStringLiteral("dmidecode"));
    if (pkexec.isEmpty() || dmi.isEmpty()) {
        m_memoryModules = QStringLiteral("Instale polkit e dmidecode para consultar SMBIOS com autorização");
        emit updated();
        return;
    }
    connect(&m_dmiProcess, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus state) {
            if (code != 0 || state != QProcess::NormalExit) {
                m_memoryModules = QStringLiteral("Autorização cancelada ou SMBIOS indisponível"); emit updated(); return;
            }
            const QString output = QString::fromUtf8(m_dmiProcess.readAllStandardOutput());
            QSet<QString> types, speeds;
            int modules = 0;
            double totalModuleGiB = 0;
            bool inMemoryDevice = false;
            for (const QString &raw : output.split('\n')) {
                const QString l = raw.trimmed();
                if (l.startsWith(QStringLiteral("Memory Device"))) inMemoryDevice = true;
                if (inMemoryDevice && l.startsWith(QStringLiteral("Size:")) &&
                    !l.contains(QStringLiteral("No Module"), Qt::CaseInsensitive)) {
                    const QRegularExpression sizeExp(QStringLiteral("^Size:\\s*([0-9]+(?:\\.[0-9]+)?)\\s*(GB|MB)"), QRegularExpression::CaseInsensitiveOption);
                    const auto match = sizeExp.match(l);
                    if (match.hasMatch() && match.captured(1).toDouble() > 0) {
                        ++modules;
                        const double amount = match.captured(1).toDouble();
                        totalModuleGiB += match.captured(2).compare(QStringLiteral("GB"), Qt::CaseInsensitive) == 0
                            ? amount : amount / 1024.0;
                    }
                }
                if (l.startsWith(QStringLiteral("Type:"))) {
                    const QString type = l.section(':',1).trimmed();
                    if (!type.isEmpty() && type != QStringLiteral("Unknown") && type != QStringLiteral("Other")) types.insert(type);
                }
                if (l.startsWith(QStringLiteral("Configured Memory Speed:"))) {
                    const QString speed = l.section(':',1).trimmed();
                    if (!speed.isEmpty() && speed != QStringLiteral("Unknown")) speeds.insert(speed);
                }
            }
            if (!types.isEmpty()) m_memoryType = QStringList(types.values()).join(QStringLiteral(", "));
            if (!speeds.isEmpty()) m_memorySpeed = QStringList(speeds.values()).join(QStringLiteral(", "));
            if (modules > 0 && totalModuleGiB > 0) m_memoryInstalledGiB = totalModuleGiB;
            m_memoryModules = modules > 0 ? QStringLiteral("%1 módulos detectados via SMBIOS").arg(modules) :
                             QStringLiteral("SMBIOS não informou módulos ativos");
            emit updated();
        }, Qt::SingleShotConnection);
    m_memoryModules = QStringLiteral("Aguardando autorização Polkit…");
    emit updated();
    m_dmiProcess.start(pkexec, {dmi, QStringLiteral("--type"), QStringLiteral("17")});
}
