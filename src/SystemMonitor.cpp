#include "TemperatureSensors.h"
#include "SataSmart.h"
#include <QtConcurrent>
#include <QDebug>
#include "SystemMonitor.h"
#include "HardwareMetadata.h"

#include <QFile>
#include <QDir>
#include <QSet>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QtGlobal>
#include <QFileInfo>
#include <QSysInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {
QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

QList<QByteArray> fields(const QByteArray &line)
{
    QList<QByteArray> values;
    for (const auto &value : line.simplified().split(' ')) {
        if (!value.isEmpty()) values.append(value);
    }
    return values;
}

double rate(quint64 current, quint64 previous, double seconds)
{
    if (seconds <= 0 || current < previous) return 0.0;
    return static_cast<double>(current - previous) / 1048576.0 / seconds;
}
}

SystemMonitor::SystemMonitor(QObject *parent) : QObject(parent)
{
    connect(&m_sataSmartWatcher, &QFutureWatcher<QHash<QString, TemperatureSensors::Reading>>::finished,
        this, [this]() {
            // Callback em thread da interface; worker não acessa m_deviceInfo.
            m_sataSmartReadings = m_sataSmartWatcher.result();
            for (auto it = m_deviceInfo.begin(); it != m_deviceInfo.end(); ++it) {
                if (it.value().value(QStringLiteral("kind")) != QStringLiteral("disk")) continue;
                const QString device = QFileInfo(it.value().value(QStringLiteral("device")).toString()).fileName();
                if (SataSmart::blockPath(device).isEmpty()) continue;
                // O hwmon/drivetemp local tem prioridade quando existe.
                const auto local = TemperatureSensors::disk(device);
                const auto smart = m_sataSmartReadings.value(device);
                const auto reading = local.valid() ? local : smart;
                it.value().insert(QStringLiteral("temperature"), reading.valid() ? reading.celsius : -1.0);
                it.value().insert(QStringLiteral("temperatureSource"), reading.valid()
                    ? reading.source : QStringLiteral("Indisponível (hwmon/S.M.A.R.T.)"));
            }
            emit updated();
        });
    const auto cpuInfo = readFile(QStringLiteral("/proc/cpuinfo"));
    for (const auto &line : cpuInfo.split('\n')) {
        if (line.startsWith("model name") || line.startsWith("Hardware")) {
            m_cpuName = QString::fromUtf8(line.mid(line.indexOf(':') + 1).trimmed());
            break;
        }
    }
    if (m_cpuName.isEmpty()) m_cpuName = QStringLiteral("CPU");
    QSet<QString> cores;
    const auto cpuFolders = QDir(QStringLiteral("/sys/devices/system/cpu"))
        .entryList({QStringLiteral("cpu[0-9]*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &folder : cpuFolders) {
        const QString root = QStringLiteral("/sys/devices/system/cpu/") + folder + QStringLiteral("/topology/");
        const auto coreId = readFile(root + QStringLiteral("core_id")).trimmed();
        const auto packageId = readFile(root + QStringLiteral("physical_package_id")).trimmed();
        if (!coreId.isEmpty()) cores.insert(QString::fromLatin1(packageId + ':' + coreId));
    }
    m_physicalCores = cores.size();

    QSet<QByteArray> socketIds;
    for (const QString &folder : cpuFolders) {
        const QString root = QStringLiteral("/sys/devices/system/cpu/") + folder + QStringLiteral("/topology/");
        const auto id = readFile(root + QStringLiteral("physical_package_id")).trimmed();
        if (!id.isEmpty()) socketIds.insert(id);
    }
    m_cpuSockets = socketIds.size();
    m_architecture = QSysInfo::currentCpuArchitecture();
    if (cpuInfo.contains(" svm ") || cpuInfo.contains(" svm\n")) m_virtualization = QStringLiteral("AMD-V / SVM");
    else if (cpuInfo.contains(" vmx ") || cpuInfo.contains(" vmx\n")) m_virtualization = QStringLiteral("Intel VT-x");
    // Contar caches físicos únicos (SMT compartilha L1/L2/L3). Exibir MiB,
    // não o tamanho de um único índice da CPU0 em KiB.
    QSet<QString> countedCaches;
    double cacheMiB[4] = {0, 0, 0, 0};
    for (const QString &cpu : cpuFolders) {
        const QString base = QStringLiteral("/sys/devices/system/cpu/") + cpu + QStringLiteral("/cache/");
        const auto indices = QDir(base).entryList({QStringLiteral("index[0-9]*")}, QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &idx : indices) {
            const QString root = base + idx + QLatin1Char('/');
            const int level = readFile(root + QStringLiteral("level")).trimmed().toInt();
            if (level < 1 || level > 3) continue;
            const QString share = QString::fromUtf8(readFile(root + QStringLiteral("shared_cpu_list")).trimmed());
            const QString type = QString::fromUtf8(readFile(root + QStringLiteral("type")).trimmed());
            const QString dedup = QString::number(level) + ':' + type + ':' + share;
            if (countedCaches.contains(dedup)) continue;
            countedCaches.insert(dedup);
            const QString size = QString::fromUtf8(readFile(root + QStringLiteral("size")).trimmed()).toUpper();
            const QRegularExpression matchSize(QStringLiteral("^([0-9]+)([KMG])$"));
            const auto match = matchSize.match(size);
            if (!match.hasMatch()) continue;
            const double amount = match.captured(1).toDouble();
            cacheMiB[level] += match.captured(2) == QStringLiteral("K") ? amount / 1024.0 :
                               match.captured(2) == QStringLiteral("M") ? amount : amount * 1024.0;
        }
    }
    auto fmtCache = [](double miB) { return miB > 0 ? QString::number(miB, 'f', miB < 1 ? 2 : 1) + QStringLiteral(" MiB") : QStringLiteral("—"); };
    m_cacheL1 = fmtCache(cacheMiB[1]);
    m_cacheL2 = fmtCache(cacheMiB[2]);
    m_cacheL3 = fmtCache(cacheMiB[3]);
    auto mhz = [](const QString &path) -> double {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return 0;
        bool ok = false;
        const double khz = f.readAll().trimmed().toDouble(&ok);
        return ok ? khz / 1000000.0 : 0;
    };
    const QString cpuRoot = QStringLiteral("/sys/devices/system/cpu/cpu0/cpufreq/");
    m_cpuBaseGHz = mhz(cpuRoot + QStringLiteral("base_frequency"));
    m_cpuMaxGHz = mhz(cpuRoot + QStringLiteral("cpuinfo_max_freq"));

    for (const auto &mc : QDir(QStringLiteral("/sys/devices/system/edac/mc"))
            .entryList({QStringLiteral("mc[0-9]*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString base = QStringLiteral("/sys/devices/system/edac/mc/") + mc;
        for (const auto &dimm : QDir(base).entryList({QStringLiteral("dimm*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString type = QString::fromUtf8(readFile(base + '/' + dimm + QStringLiteral("/dimm_mem_type")).trimmed());
            if (!type.isEmpty() && type != QStringLiteral("Unknown")) { m_memoryType = type; break; }
        }
    }
    enumerateGpus();
    m_nmTimer.setInterval(60000);
    connect(&m_nmTimer, &QTimer::timeout, this, &SystemMonitor::pollNetworkNames);
    connect(&m_nmProcess, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus state) {
            if (code != 0 || state != QProcess::NormalExit) return;
            for (const auto &line : QString::fromUtf8(m_nmProcess.readAllStandardOutput()).split('\n')) {
                const int colon = line.indexOf(':');
                if (colon < 1) continue;
                const QString device = line.left(colon).trimmed();
                const QString name = line.mid(colon+1).trimmed().replace(QStringLiteral("\\:"), QStringLiteral(":"));
                if (device.isEmpty() || name.isEmpty() || name == QStringLiteral("--")) continue;
                m_connections[device] = name;
                const QString key = QStringLiteral("net:") + device;
                if (m_deviceInfo.contains(key)) m_deviceInfo[key].insert(QStringLiteral("connectionName"), name);
            }
            emit updated();
        });
    m_timer.setInterval(m_intervalMs);
    connect(&m_timer, &QTimer::timeout, this, &SystemMonitor::poll);
    m_gpuTimer.setInterval(3000);
    connect(&m_gpuTimer, &QTimer::timeout, this, &SystemMonitor::pollGpu);
    connect(&m_gpuProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this](int exitCode, QProcess::ExitStatus status) {
            if (status != QProcess::NormalExit || exitCode != 0) { m_gpuAvailable = false; emit updated(); return; }
            const auto lines = QString::fromUtf8(m_gpuProcess.readAllStandardOutput()).trimmed().split('\n');
            bool found = false;
            int index = 0;
            for (const auto &line : lines) {
                const auto v = line.split(',');
                if (v.size() < 7) continue;
                bool ok = false;
                const double usage = v[2].trimmed().toDouble(&ok);
                if (!ok) continue;
                found = true;
                QString pci = v[6].trimmed().toLower();
                const auto match = QRegularExpression(QStringLiteral("([0-9a-f]{2}:[0-9a-f]{2}\\.[0-7])$")).match(pci);
                if (match.hasMatch()) pci = match.captured(1);
                QString key = m_gpuPciToKey.value(pci);
                if (key.isEmpty()) key = QStringLiteral("gpu:nvidia-") + QString::number(index);
                const double percent = std::clamp(usage, 0.0, 100.0);
                upsertDevice(key, {{"kind", "gpu"}, {"label", QStringLiteral("GPU NVIDIA ") + QString::number(index)},
                    {"model", v[0].trimmed()}, {"driver", v[1].trimmed()}, {"percent", percent},
                    {"memoryUsedMiB", v[3].trimmed().toDouble()}, {"memoryTotalMiB", v[4].trimmed().toDouble()},
                    {"temperature", v[5].trimmed().toInt()}, {"pci", v[6].trimmed()},
                    {"subtitle", QStringLiteral("%1% · %2").arg(percent, 0, 'f', 0).arg(v[0].trimmed())},
                    {"active", true}});
                appendHistory(m_deviceHistories[key], percent);
                if (index == 0) {
                    m_gpuName = v[0].trimmed();
                    m_gpuDriver = v[1].trimmed();
                    m_gpuPercent = percent;
                    m_gpuMemoryUsedMiB = v[3].trimmed().toDouble();
                    m_gpuMemoryTotalMiB = v[4].trimmed().toDouble();
                    m_gpuTemperatureC = v[5].trimmed().toInt();
                }
                ++index;
            }
            m_gpuAvailable = found;
            emit updated();
        });
    connect(&m_gpuEngineProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this](int exitCode, QProcess::ExitStatus status) {
            if (status != QProcess::NormalExit || exitCode != 0) return;
            for (const auto &line : QString::fromUtf8(m_gpuEngineProcess.readAllStandardOutput()).trimmed().split('\n')) {
                const auto values = line.split(',');
                if (values.size() < 3) continue;
                QString pci = values[0].trimmed().toLower();
                const auto match = QRegularExpression(QStringLiteral("([0-9a-f]{2}:[0-9a-f]{2}\\.[0-7])$")).match(pci);
                if (match.hasMatch()) pci = match.captured(1);
                const QString key = m_gpuPciToKey.value(pci);
                if (key.isEmpty()) continue;
                bool encOk = false, decOk = false;
                const double enc = values[1].trimmed().toDouble(&encOk);
                const double dec = values[2].trimmed().toDouble(&decOk);
                if (encOk) {
                    m_deviceInfo[key].insert(QStringLiteral("encodePercent"), enc);
                    appendHistory(m_deviceHistories[key + QStringLiteral("#encodePercent")], enc);
                }
                if (decOk) {
                    m_deviceInfo[key].insert(QStringLiteral("decodePercent"), dec);
                    appendHistory(m_deviceHistories[key + QStringLiteral("#decodePercent")], dec);
                }
            }
            emit updated();
        });

}

void SystemMonitor::setIntervalMs(int value)
{
    const int bounded = std::clamp(value, 500, 10000);
    if (bounded == m_intervalMs) return;
    m_intervalMs = bounded;
    m_timer.setInterval(m_intervalMs);
    emit intervalChanged();
}

void SystemMonitor::loadMemoryUdev()
{
    if (m_udevProcess.state() != QProcess::NotRunning) return;
    const QString udevadm = QStandardPaths::findExecutable(QStringLiteral("udevadm"));
    if (udevadm.isEmpty()) return;
    connect(&m_udevProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            if (code != 0 || status != QProcess::NormalExit) return;
            const auto info = HardwareMetadata::parseMemoryUdev(m_udevProcess.readAllStandardOutput());
            if (info.installedGiB <= 0) return;
            m_memoryInstalledGiB = info.installedGiB;
            if (!info.type.isEmpty()) m_memoryType = info.type;
            if (!info.configuredSpeed.isEmpty()) m_memorySpeed = info.configuredSpeed;
            if (!info.modulesText.isEmpty()) m_memoryModules = info.modulesText;
            if (!info.slotsText.isEmpty()) m_memorySlots = info.slotsText;
            emit updated();
        }, Qt::SingleShotConnection);
    m_udevProcess.start(udevadm, {QStringLiteral("info"), QStringLiteral("--query=property"),
                                 QStringLiteral("--path=/sys/devices/virtual/dmi/id")});
    QTimer::singleShot(2500, this, [this]() {
        if (m_udevProcess.state() != QProcess::NotRunning) m_udevProcess.kill();
    });
}

void SystemMonitor::start()
{
    loadMemoryUdev();
    poll();
    m_timer.start();
    pollGraphicsApi();
    pollNetworkNames();
    m_nmTimer.start();
    if (!QStandardPaths::findExecutable(QStringLiteral("nvidia-smi")).isEmpty()) {
        pollGpu();
        m_gpuTimer.start();
    }
}

void SystemMonitor::appendHistory(QVector<double> &series, double value)
{
    series.append(value);
    if (series.size() > 60) series.removeFirst();
}

QVariantList SystemMonitor::list(const QVector<double> &values)
{
    QVariantList result;
    result.reserve(values.size());
    for (double value : values) result.append(value);
    return result;
}

QVariantList SystemMonitor::cpuHistory() const { return list(m_cpuHistory); }
QVariantList SystemMonitor::memoryHistory() const { return list(m_memoryHistory); }
QVariantList SystemMonitor::swapHistory() const { return list(m_swapHistory); }
QVariantList SystemMonitor::diskHistory() const { return list(m_diskHistory); }
QVariantList SystemMonitor::networkHistory() const { return list(m_networkHistory); }
QVariantList SystemMonitor::gpuHistory() const { return list(m_gpuHistory); }

QVariantList SystemMonitor::perCoreHistories() const
{
    QVariantList result;
    result.reserve(m_coreHistories.size());
    for (const auto &history : m_coreHistories) result.append(list(history));
    return result;
}

void SystemMonitor::pollGpu()
{
    if (m_gpuProcess.state() == QProcess::NotRunning) {
        m_gpuProcess.start(QStringLiteral("nvidia-smi"), {
            QStringLiteral("--query-gpu=name,driver_version,utilization.gpu,memory.used,memory.total,temperature.gpu,pci.bus_id"),
            QStringLiteral("--format=csv,noheader,nounits")
        });
        QTimer::singleShot(2400, this, [this]() {
            if (m_gpuProcess.state() != QProcess::NotRunning) m_gpuProcess.kill();
        });
    }
    if (m_gpuEngineProcess.state() == QProcess::NotRunning) {
        m_gpuEngineProcess.start(QStringLiteral("nvidia-smi"), {
            QStringLiteral("--query-gpu=pci.bus_id,utilization.encoder,utilization.decoder"),
            QStringLiteral("--format=csv,noheader,nounits")
        });
        QTimer::singleShot(2400, this, [this]() {
            if (m_gpuEngineProcess.state() != QProcess::NotRunning) m_gpuEngineProcess.kill();
        });
    }
}

// Dados pequenos e sem privilégios ficam disponíveis já no primeiro frame.
// Não invocar process sampling, busca de ícones, comandos externos nem varredura
// hwmon/SMART nesta etapa. O monitoramento completo continua normalmente depois.
void SystemMonitor::primeSnapshot()
{
    QElapsedTimer stopwatch;
    const bool trace = qEnvironmentVariableIsSet("TAREFAS11_STARTUP_TRACE");
    if (trace) stopwatch.start();

    sampleMemory();
    const auto uptime = fields(readFile(QStringLiteral("/proc/uptime")));
    if (!uptime.isEmpty()) m_uptimeSeconds = uptime[0].toDouble();

    // Estabelece uma amostra anterior real para que o primeiro percentual de
    // CPU represente o intervalo desde a abertura, e não um 0% fictício.
    const auto lines = readFile(QStringLiteral("/proc/stat")).split('\n');
    for (const auto &line : lines) {
        if (!line.startsWith("cpu")) break;
        const auto tokens = fields(line);
        if (tokens.size() < 5) continue;
        CpuTicks sample;
        for (int i = 1; i < tokens.size(); ++i)
            sample.total += tokens[i].toULongLong();
        sample.idle = tokens[4].toULongLong();
        if (tokens.size() > 5) sample.idle += tokens[5].toULongLong();
        if (tokens[0] == "cpu") m_lastCpu = sample;
        else m_lastCores.append(sample);
    }
    m_coreHistories.resize(m_lastCores.size());

    // Popula os cartões por dispositivo (disco/rede) sem ler sensores SMART ou
    // resolver identidades de centenas de processos. Contadores de taxa só
    // aparecem depois que o segundo valor real é observado.
    const double baselineSeconds = static_cast<double>(m_intervalMs) / 1000.0;
    sampleDisks(baselineSeconds);
    sampleNetworks(baselineSeconds);
    appendHistory(m_memoryHistory, m_memoryPercent);
    appendHistory(m_swapHistory, m_swapTotalGiB > 0
        ? 100.0 * m_swapUsedGiB / m_swapTotalGiB : 0.0);
    m_elapsed.start();
    emit updated();
    if (trace)
        qInfo().noquote() << "[Tarefas11 startup] prévia RAM/SWAP/discos/rede:"
                          << stopwatch.elapsed() << "ms";
}

// Usada tanto na prévia quanto na coleta periódica para garantir que os dados
// iniciais e os posteriores usem exatamente a mesma definição de memória.
void SystemMonitor::sampleMemory()
{
    QHash<QByteArray, quint64> mem;
    for (const auto &line : readFile(QStringLiteral("/proc/meminfo")).split('\n')) {
        const auto colon = line.indexOf(':');
        if (colon <= 0) continue;
        const auto parts = fields(line.mid(colon + 1));
        if (!parts.isEmpty()) mem.insert(line.left(colon), parts[0].toULongLong());
    }
    const double totalKiB = static_cast<double>(mem.value("MemTotal"));
    const double availableKiB = static_cast<double>(mem.value("MemAvailable"));
    const double swapKiB = static_cast<double>(mem.value("SwapTotal"));
    const double swapFreeKiB = static_cast<double>(mem.value("SwapFree"));
    m_memoryAvailableGiB = availableKiB / 1048576.0;
    m_memoryCachedGiB = double(mem.value("Cached")) / 1048576.0;
    m_memoryTotalGiB = totalKiB / 1048576.0;
    m_memoryUsedGiB = std::max(0.0, totalKiB - availableKiB) / 1048576.0;
    m_memoryPercent = totalKiB > 0 ? 100.0 * (totalKiB - availableKiB) / totalKiB : 0;
    m_swapTotalGiB = swapKiB / 1048576.0;
    m_swapUsedGiB = std::max(0.0, swapKiB - swapFreeKiB) / 1048576.0;
    m_memoryFreeGiB = double(mem.value("MemFree")) / 1048576.0;
    m_memoryBuffersGiB = double(mem.value("Buffers")) / 1048576.0;
    m_memorySlabGiB = double(mem.value("Slab")) / 1048576.0;
    m_memorySharedGiB = double(mem.value("Shmem")) / 1048576.0;
    m_memoryCommittedGiB = double(mem.value("Committed_AS")) / 1048576.0;
    m_memoryActiveGiB = double(mem.value("Active")) / 1048576.0;
    m_zramOriginalGiB = m_zramCompressedGiB = m_zramPhysicalGiB = 0;
    for (const QString &dev : QDir(QStringLiteral("/sys/block")).entryList({QStringLiteral("zram[0-9]*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto values = fields(readFile(QStringLiteral("/sys/block/") + dev + QStringLiteral("/mm_stat")));
        if (values.size() < 3) continue;
        m_zramOriginalGiB += values[0].toDouble() / (1024.0 * 1024.0 * 1024.0);
        m_zramCompressedGiB += values[1].toDouble() / (1024.0 * 1024.0 * 1024.0);
        m_zramPhysicalGiB += values[2].toDouble() / (1024.0 * 1024.0 * 1024.0);
    }
}

void SystemMonitor::poll()
{
    static int traced = 0;
    const bool trace = traced < 3 && qEnvironmentVariableIsSet("TAREFAS11_STARTUP_TRACE");
    QElapsedTimer elapsed;
    if (trace) elapsed.start();
    double seconds = static_cast<double>(m_intervalMs) / 1000.0;
    if (m_elapsed.isValid())
        seconds = std::max(0.001, static_cast<double>(m_elapsed.restart()) / 1000.0);
    else
        m_elapsed.start();

    quint64 totalCpuTicks = 0;
    const auto statLines = readFile(QStringLiteral("/proc/stat")).split('\n');
    QVector<CpuTicks> cores;
    for (const auto &line : statLines) {
        if (!line.startsWith("cpu")) break;
        const auto tokens = fields(line);
        if (tokens.size() < 5) continue;
        CpuTicks now;
        for (int i = 1; i < tokens.size(); ++i) now.total += tokens[i].toULongLong();
        now.idle = tokens[4].toULongLong();
        if (tokens.size() > 5) now.idle += tokens[5].toULongLong();
        if (tokens[0] == "cpu") {
            totalCpuTicks = now.total;
            if (m_lastCpu.total > 0 && now.total > m_lastCpu.total) {
                const auto deltaTotal = now.total - m_lastCpu.total;
                const auto deltaIdle = now.idle >= m_lastCpu.idle ? now.idle - m_lastCpu.idle : 0;
                m_cpuPercent = std::clamp(100.0 * (1.0 - static_cast<double>(deltaIdle) /
                                                 static_cast<double>(deltaTotal)), 0.0, 100.0);
            }
            m_lastCpu = now;
        } else {
            cores.append(now);
        }
    }
    if (m_coreHistories.size() != cores.size()) {
        m_coreHistories.resize(cores.size());
        m_lastCores.resize(cores.size());
    }
    for (int i = 0; i < cores.size(); ++i) {
        double value = 0;
        if (m_lastCores[i].total > 0 && cores[i].total > m_lastCores[i].total) {
            const auto total = cores[i].total - m_lastCores[i].total;
            const auto idle = cores[i].idle >= m_lastCores[i].idle
                    ? cores[i].idle - m_lastCores[i].idle : 0;
            value = std::clamp(100.0 * (1.0 - static_cast<double>(idle) /
                                           static_cast<double>(total)), 0.0, 100.0);
        }
        appendHistory(m_coreHistories[i], value);
    }
    m_lastCores = cores;

    sampleMemory();

    if (!m_ramTempTimer.isValid() || m_ramTempTimer.elapsed() >= 5000) {
        const auto reading = HardwareMetadata::ramTemperatures();
        m_memoryTemperature = reading.display;
        m_memoryTemperatureSensors = reading.sensorCount;
        m_ramTempTimer.restart();
    }

    sampleDisks(seconds);
    // hwmon é leitura local sem privilégios; sondar em baixa frequência evita I/O
    // repetitivo em centenas de PIDs e associa temperatura ao disco certo.
    if (!m_temperatureTimer.isValid() || m_temperatureTimer.elapsed() >= 5000) {
        const auto cpuSensor = TemperatureSensors::cpu();
        m_cpuTemperatureC = cpuSensor.valid() ? cpuSensor.celsius : -1.0;
        for (auto it = m_deviceInfo.begin(); it != m_deviceInfo.end(); ++it) {
            if (it.value().value(QStringLiteral("kind")) != QStringLiteral("disk")) continue;
            const QString device = it.value().value(QStringLiteral("device")).toString();
            const QString name = QFileInfo(device).fileName();
            const auto local = TemperatureSensors::disk(name);
            const auto smart = m_sataSmartReadings.value(name);
            const auto reading = local.valid() ? local : smart;
            it.value().insert(QStringLiteral("temperature"), reading.valid() ? reading.celsius : -1.0);
            it.value().insert(QStringLiteral("temperatureSource"),
                              reading.valid() ? reading.source : QStringLiteral("Indisponível (hwmon/S.M.A.R.T.)"));
        }
        m_temperatureTimer.restart();
    }
    // UDisks2 pode fazer E/S com SMART; nunca bloquear a thread gráfica.
    // SMART pode ficar em cache no daemon; não forçar refresh nem despertar discos.
    if (!m_sataSmartWatcher.isRunning() &&
        (!m_sataSmartTimer.isValid() || m_sataSmartTimer.elapsed() >= 30000)) {
        QStringList sataDevices;
        for (auto it = m_deviceInfo.cbegin(); it != m_deviceInfo.cend(); ++it) {
            if (it.value().value(QStringLiteral("kind")) != QStringLiteral("disk")) continue;
            const QString device = QFileInfo(it.value().value(QStringLiteral("device")).toString()).fileName();
            if (!SataSmart::blockPath(device).isEmpty()) sataDevices.append(device);
        }
        m_sataSmartTimer.start();
        if (!sataDevices.isEmpty()) {
            m_sataSmartWatcher.setFuture(QtConcurrent::run([sataDevices]() {
                return SataSmart::sample(sataDevices);
            }));
        }
    }
    sampleNetworks(seconds);
    sampleGpus();

    const auto openFileFields = fields(readFile(QStringLiteral("/proc/sys/fs/file-nr")));
    if (!openFileFields.isEmpty()) m_systemOpenFiles = openFileFields[0].toULongLong();
    const auto uptime = fields(readFile(QStringLiteral("/proc/uptime")));
    if (!uptime.isEmpty()) m_uptimeSeconds = uptime[0].toDouble();

    // Frequência média observada nos núcleos lógicos com cpufreq disponível.
    double frequencySum = 0;
    int frequencySamples = 0;
    for (int cpu = 0; cpu < cores.size(); ++cpu) {
        const QByteArray clock = readFile(QStringLiteral("/sys/devices/system/cpu/cpu%1/cpufreq/scaling_cur_freq").arg(cpu)).trimmed();
        bool ok = false;
        const double khz = clock.toDouble(&ok);
        if (ok && khz > 0) { frequencySum += khz; ++frequencySamples; }
    }
    if (frequencySamples) m_cpuFrequencyGHz = frequencySum / frequencySamples / 1000000.0;

    appendHistory(m_cpuHistory, m_cpuPercent);
    appendHistory(m_memoryHistory, m_memoryPercent);
    appendHistory(m_swapHistory, m_swapTotalGiB > 0 ? 100.0 * m_swapUsedGiB / m_swapTotalGiB : 0.0);
    appendHistory(m_diskHistory, m_diskReadMBs + m_diskWriteMBs);
    appendHistory(m_networkHistory, m_netRxMBs + m_netTxMBs);
    appendHistory(m_gpuHistory, m_gpuPercent);

    emit updated();
    const qint64 hardwareMs = trace ? elapsed.elapsed() : 0;
    emit sampled(totalCpuTicks);
    if (trace) {
        ++traced;
        qInfo().noquote() << "[Tarefas11 startup] hardware:"
                          << hardwareMs << "ms; hardware + processos:"
                          << elapsed.elapsed() << "ms";
    }
}

void SystemMonitor::pollNetworkNames() {
    if (m_nmProcess.state() != QProcess::NotRunning) return;
    const QString nmcli = QStandardPaths::findExecutable(QStringLiteral("nmcli"));
    if (nmcli.isEmpty()) return;
    m_nmProcess.start(nmcli, {QStringLiteral("-t"), QStringLiteral("-f"),
                            QStringLiteral("DEVICE,CONNECTION"), QStringLiteral("device"), QStringLiteral("status")});
    QTimer::singleShot(2300, this, [this]() {
        if (m_nmProcess.state() != QProcess::NotRunning) m_nmProcess.kill();
    });
}
