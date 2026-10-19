#include "ProcessModel.h"
#include "MemoryMetrics.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QDebug>
#include <QUrl>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <functional>
#include <utility>
#include <pwd.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

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

quint64 processStartTime(int pid)
{
    const auto stat = readFile(QStringLiteral("/proc/%1/stat").arg(pid));
    const auto closing = stat.lastIndexOf(')');
    if (closing < 0) return 0;
    const auto parts = fields(stat.mid(closing + 1));
    return parts.size() > 19 ? parts[19].toULongLong() : 0;
}

bool signalProcess(int pid, int number)
{
    return ::kill(pid, number) == 0;
}
}

ProcessModel::ProcessModel(AppIdentityResolver *resolver, QObject *parent)
    : QAbstractListModel(parent), m_resolver(resolver)
{
    // Nenhuma resolução de ícone bloqueia a primeira amostragem de /proc.
    m_identityTimer.setSingleShot(true);
    QObject::connect(&m_identityTimer, &QTimer::timeout, this,
                     &ProcessModel::resolveIdentityBatch);
    m_resolver->setIndexReadyCallback([this]() { desktopIndexAvailable(); });
}

int ProcessModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant ProcessModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const auto &row = m_rows.at(index.row());

    if (role == RowTypeRole) return row.header ? QStringLiteral("header") : QStringLiteral("process");
    if (role == IsHeaderRole) return row.header;
    if (role == CategoryKeyRole) return row.categoryKey;
    if (role == CategoryTitleRole) return row.categoryTitle;
    if (role == LevelRole) return row.level;
    if (role == ExpandableRole) return row.expandable;
    if (role == ExpandedRole) return row.expanded;
    if (role == ChildCountRole) return row.childCount;
    if (role == AggregatedRole) return row.aggregated;
    if (role == GroupProcessCountRole) return row.groupProcessCount;
    if (role == OwnMemoryRole) return row.ownMemoryMiB;

    if (row.header) {
        switch (role) {
        case DisplayNameRole: return row.categoryTitle;
        case ProcessNameRole: return row.categoryTitle;
        default: return {};
        }
    }

    const auto &p = row.entry;
    switch (role) {
    case PidRole: return p.pid;
    case ProcessNameRole: return p.name;
    case CpuRole: return p.cpu;
    case MemoryRole: return p.memoryMiB;
    case SwapMiBRole: return p.swapMiB;
    case SwapAvailableRole: return p.swapAvailable;
    case DiskRole: return p.diskMiBs;
    case IoShareRole: return p.ioSharePercent;
    case NetworkRateRole: return p.networkMiBs;
    case NetworkAvailableRole: return p.networkAvailable;
    case NetworkRxRole: return p.networkRxMiBs;
    case NetworkTxRole: return p.networkTxMiBs;
    case IoAvailableRole: return p.ioAvailable;
    case UserRole: return p.user;
    case StateRole: return p.state;
    case CommandRole: return p.command;
    case StartTimeRole: return QVariant::fromValue(static_cast<qulonglong>(p.startTime));
    case ThreadsRole: return p.threads;
    case DisplayNameRole: return p.identity.displayName;
    case IconUrlRole: return p.identity.iconUrl;
    case IconSourceRole: return p.identity.iconSource;
    case PlatformRole: return p.identity.platform;
    case ExePathRole: return p.identity.executablePath.isEmpty() ? QString{} : p.identity.executablePath;
    case DesktopFileRole: return p.identity.desktopFile;
    case PpidRole: return p.ppid;
    case NiceRole: return p.nice;
    case VirtualMiBRole: return p.virtualMiB;
    case SharedMiBRole: return p.sharedMiB;
    case ReadMiBsRole: return p.readMiBs;
    case WriteMiBsRole: return p.writeMiBs;
    case CpuTimeSecondsRole: return p.cpuTimeSeconds;
    case IsAppRole: return p.identity.desktopMatched || p.identity.windows;
    case IsWindowsRole: return p.identity.windows;
    default: return {};
    }
}

QHash<int, QByteArray> ProcessModel::roleNames() const
{
    return {
        { PidRole, "pid" }, { ProcessNameRole, "processName" },
        { CpuRole, "cpu" }, { MemoryRole, "memoryMiB" },
        { SwapMiBRole, "swapMiB" }, { SwapAvailableRole, "swapAvailable" },
        { DiskRole, "diskMiBs" },
        { IoShareRole, "ioSharePercent" }, { NetworkRateRole, "networkMiBs" },
        { NetworkAvailableRole, "networkAvailable" },
        { NetworkRxRole, "networkRxMiBs" }, { NetworkTxRole, "networkTxMiBs" }, { IoAvailableRole, "ioAvailable" },
        { UserRole, "processUser" }, { StateRole, "processState" },
        { CommandRole, "command" }, { StartTimeRole, "startTime" },
        { ThreadsRole, "threads" },
        { DisplayNameRole, "displayName" }, { IconUrlRole, "iconUrl" },
        { IconSourceRole, "iconSource" }, { PlatformRole, "platform" },
        { ExePathRole, "exePath" }, { DesktopFileRole, "desktopFile" },
        { PpidRole, "ppid" }, { NiceRole, "niceValue" },
        { VirtualMiBRole, "virtualMiB" }, { SharedMiBRole, "sharedMiB" },
        { ReadMiBsRole, "readMiBs" }, { WriteMiBsRole, "writeMiBs" },
        { CpuTimeSecondsRole, "cpuTimeSeconds" },
        { IsAppRole, "isApp" }, { IsWindowsRole, "isWindows" },
        { RowTypeRole, "rowType" }, { IsHeaderRole, "isHeader" },
        { CategoryKeyRole, "categoryKey" }, { CategoryTitleRole, "categoryTitle" },
        { LevelRole, "level" }, { ExpandableRole, "expandable" },
        { ExpandedRole, "expanded" }, { ChildCountRole, "childCount" },
        { AggregatedRole, "aggregated" }, { GroupProcessCountRole, "groupProcessCount" },
        { OwnMemoryRole, "ownMemoryMiB" }
    };
}

void ProcessModel::setFilterText(const QString &value)
{
    if (m_filter == value) return;
    m_filter = value;
    rebuild();
    emit filterChanged();
}

void ProcessModel::setTypeFilter(const QString &value)
{
    if (m_typeFilter == value) return;
    m_typeFilter = value;
    rebuild();
    emit typeFilterChanged();
}

void ProcessModel::setSortField(const QString &value)
{
    if (m_sortField == value) return;
    m_sortField = value;
    rebuild();
    emit sortChanged();
}

void ProcessModel::setGroupMode(bool value)
{
    if (m_groupMode == value) return;
    m_groupMode = value;
    rebuild();
    emit groupModeChanged();
}

QString ProcessModel::processKey(int pid, quint64 startTime) const
{
    return QString::number(pid) + QLatin1Char(':') + QString::number(startTime);
}

QString ProcessModel::classifyCategory(const Entry &entry) const
{
    if (entry.identity.windows) return QStringLiteral("windows");
    if (entry.identity.desktopMatched) return QStringLiteral("applications");
    return QStringLiteral("background");
}

QString ProcessModel::categoryTitle(const QString &key) const
{
    if (key == QStringLiteral("applications")) return QStringLiteral("Aplicativos");
    if (key == QStringLiteral("windows")) return QStringLiteral("Programas Windows");
    return QStringLiteral("Processos em segundo plano");
}

void ProcessModel::refresh(quint64 hostCpuTicks)
{
    static int traced = 0;
    const bool trace = traced < 3 && qEnvironmentVariableIsSet("TAREFAS11_STARTUP_TRACE");
    QElapsedTimer elapsed;
    if (trace) elapsed.start();
    qint64 nssNs = 0, quickIdentityNs = 0;
    const double seconds = m_elapsed.isValid()
        ? std::max(0.001, static_cast<double>(m_elapsed.restart()) / 1000.0)
        : 1.5;
    if (!m_elapsed.isValid()) m_elapsed.start();

    const quint64 deltaHost = hostCpuTicks > m_previousHostCpu
        ? hostCpuTicks - m_previousHostCpu : 0;

    QHash<int, quint64> nextCpu;
    QHash<int, quint64> nextStartTime;
    QHash<int, quint64> nextRead, nextWrite;
    QHash<int, CachedIdentity> nextIdentities;
    int windows = 0, threadTotal = 0;
    QVector<Entry> entries;
    const auto procDirs = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    entries.reserve(procDirs.size());
    for (const auto &pidString : procDirs) {
        bool ok = false;
        const int pid = pidString.toInt(&ok);
        if (!ok || pid <= 0) continue;
        const QString base = QStringLiteral("/proc/%1/").arg(pid);
        const auto stat = readFile(base + QStringLiteral("stat"));
        const int opening = stat.indexOf('(');
        const int closing = stat.lastIndexOf(')');
        if (opening < 0 || closing <= opening) continue;
        const auto parts = fields(stat.mid(closing + 1));
        if (parts.size() <= 20) continue;

        Entry entry;
        entry.pid = pid;
        entry.name = QString::fromUtf8(stat.mid(opening + 1, closing - opening - 1));
        entry.startTime = parts[19].toULongLong();
        entry.ppid = parts[1].toInt();
        entry.nice = parts[16].toInt();
        static const long clockTicks = std::max<long>(1, sysconf(_SC_CLK_TCK));
        entry.cpuTicks = parts[11].toULongLong() + parts[12].toULongLong();
        entry.threads = parts[17].toInt();
        entry.cpuTimeSeconds = double(entry.cpuTicks) / clockTicks;
        entry.virtualMiB = double(parts[20].toULongLong()) / 1048576.0;
        const char state = parts[0].isEmpty() ? '?' : parts[0].at(0);
        switch (state) {
        case 'R': entry.state = QStringLiteral("Executando"); break;
        case 'S': entry.state = QStringLiteral("Aguardando"); break;
        case 'D': entry.state = QStringLiteral("E/S"); break;
        case 'T': case 't': entry.state = QStringLiteral("Parado"); break;
        case 'Z': entry.state = QStringLiteral("Zumbi"); break;
        default: entry.state = QStringLiteral("Outro"); break;
        }
        if (deltaHost > 0 && m_previousCpu.contains(pid) &&
            m_previousStartTime.value(pid) == entry.startTime) {
            const auto previous = m_previousCpu.value(pid);
            if (entry.cpuTicks >= previous)
                entry.cpu = std::clamp(100.0 * static_cast<double>(entry.cpuTicks - previous) /
                                       static_cast<double>(deltaHost), 0.0, 100.0);
        }
        nextCpu.insert(pid, entry.cpuTicks);
        nextStartTime.insert(pid, entry.startTime);

        const auto status = readFile(base + QStringLiteral("status"));
        const auto memory = MemoryMetrics::parseProcessStatus(status);
        entry.memoryMiB = memory.residentMiB;
        entry.swapMiB = memory.swapMiB;
        entry.swapAvailable = memory.swapAvailable;
        uint uid = static_cast<uint>(-1);
        for (const auto &line : status.split('\n')) {
            if (line.startsWith("RssFile:")) {
                const auto values = fields(line.mid(8));
                if (!values.isEmpty()) entry.sharedMiB += values[0].toDouble() / 1024.0;
            } else if (line.startsWith("RssShmem:")) {
                const auto values = fields(line.mid(9));
                if (!values.isEmpty()) entry.sharedMiB += values[0].toDouble() / 1024.0;
            } else if (line.startsWith("Uid:")) {
                const auto values = fields(line.mid(4));
                if (!values.isEmpty()) uid = values[0].toUInt();
            }
        }
        if (uid != static_cast<uint>(-1)) {
            if (!m_userNames.contains(uid)) {
                QElapsedTimer nssClock;
                if (trace) nssClock.start();
                if (const auto *pw = getpwuid(static_cast<uid_t>(uid)); pw && pw->pw_name)
                    m_userNames.insert(uid, QString::fromLocal8Bit(pw->pw_name));
                else
                    m_userNames.insert(uid, QString::number(uid));
                if (trace) nssNs += nssClock.nsecsElapsed();
            }
            entry.user = m_userNames.value(uid);
        } else {
            entry.user = QStringLiteral("—");
        }

        const auto cmdline = readFile(base + QStringLiteral("cmdline"));
        const auto argBytes = cmdline.split('\0');
        QStringList args;
        for (const auto &arg : argBytes) {
            if (!arg.isEmpty()) args.append(QString::fromLocal8Bit(arg));
        }
        entry.command = args.join(' ');
        if (entry.command.isEmpty()) entry.command = entry.name;
        entry.arguments = std::move(args);
        entry.identitySignature = entry.name + QLatin1Char('|') + entry.command.left(1024);
        const bool indexReady = m_resolver->isDesktopIndexReady();
        const auto previousIdentity = m_identityCache.constFind(pid);
        const bool sameProcess = previousIdentity != m_identityCache.cend() &&
            previousIdentity->startTime == entry.startTime &&
            previousIdentity->signature == entry.identitySignature;
        const bool currentIndex = sameProcess && (!indexReady || previousIdentity->desktopIndexed);
        const bool complete = sameProcess && currentIndex && previousIdentity->complete;
        if (sameProcess && currentIndex) {
            entry.identity = previousIdentity->value;
        } else {
            // Não consultar /proc/PID/exe, ícones do KDE ou PE na varredura principal.
            QElapsedTimer quickClock;
            if (trace) quickClock.start();
            entry.identity = m_resolver->quickIdentity(entry.name, entry.arguments);
            if (trace) quickIdentityNs += quickClock.nsecsElapsed();
        }
        nextIdentities.insert(pid, {entry.startTime, entry.identitySignature,
                                    entry.identity, complete, indexReady});
        if (entry.identity.windows) ++windows;
        threadTotal += entry.threads;

        const auto io = readFile(base + QStringLiteral("io"));
        if (!io.isEmpty()) {
            entry.ioAvailable = true;
            for (const auto &line : io.split('\n')) {
                if (line.startsWith("read_bytes:")) entry.readBytes = line.mid(11).trimmed().toULongLong();
                if (line.startsWith("write_bytes:")) entry.writeBytes = line.mid(12).trimmed().toULongLong();
            }
            if (m_previousStartTime.value(pid) == entry.startTime) {
                if (m_previousRead.contains(pid) && entry.readBytes >= m_previousRead.value(pid))
                    entry.readMiBs = double(entry.readBytes - m_previousRead.value(pid)) / (1048576.0 * seconds);
                if (m_previousWrite.contains(pid) && entry.writeBytes >= m_previousWrite.value(pid))
                    entry.writeMiBs = double(entry.writeBytes - m_previousWrite.value(pid)) / (1048576.0 * seconds);
            }
            entry.diskMiBs = entry.readMiBs + entry.writeMiBs;
            nextRead.insert(pid, entry.readBytes);
            nextWrite.insert(pid, entry.writeBytes);
        }
        entries.append(std::move(entry));
    }
    const qint64 enumerationMs = trace ? elapsed.elapsed() : 0;

    // Participação em bytes de E/S dos processos acessíveis; NÃO é utilização física do disco.
    double ioTotal = 0;
    QVector<int> allPids;
    allPids.reserve(entries.size());
    for (const auto &e : std::as_const(entries)) {
        if (e.ioAvailable) ioTotal += e.diskMiBs;
        allPids.append(e.pid);
    }
    m_tcpSampler.update(allPids);
    for (auto &e : entries) {
        if (ioTotal > 0 && e.ioAvailable) e.ioSharePercent = 100.0 * e.diskMiBs / ioTotal;
        const auto rate = m_tcpSampler.rateForPid(e.pid);
        e.networkAvailable = rate.valid;
        e.networkRxMiBs = rate.rxMiBs;
        e.networkTxMiBs = rate.txMiBs;
        e.networkMiBs = rate.rxMiBs + rate.txMiBs;
    }

    m_previousHostCpu = hostCpuTicks;
    m_previousCpu = std::move(nextCpu);
    m_previousStartTime = std::move(nextStartTime);
    m_previousRead = std::move(nextRead);
    m_previousWrite = std::move(nextWrite);
    m_identityCache = std::move(nextIdentities);
    const int oldCount = m_all.size();
    const int oldWindows = m_windowsCount, oldThreads = m_threadCount;
    m_windowsCount = windows;
    m_threadCount = threadTotal;
    m_all = std::move(entries);
    // Limita estados de expansão aos processos ainda existentes (PID + startTime).
    QSet<QString> activeKeys;
    for (const Entry &entry : std::as_const(m_all))
        activeKeys.insert(processKey(entry.pid, entry.startTime));
    for (auto it = m_expanded.begin(); it != m_expanded.end();) {
        if (!activeKeys.contains(*it)) it = m_expanded.erase(it);
        else ++it;
    }
    rebuild();
    scheduleIdentityUpdates();
    if (oldCount != m_all.size() || oldWindows != m_windowsCount ||
        oldThreads != m_threadCount) emit totalCountChanged();
    emit selectedInfoChanged();
    if (trace) {
        ++traced;
        qInfo().noquote() << "[Tarefas11 startup] PIDs e métricas (ícones adiados):"
                          << enumerationMs << "ms; atualização da tabela:"
                          << elapsed.elapsed() << "ms; PIDs:" << m_all.size()
                          << "; identidades pendentes:" << m_identityQueue.size()
                          << "; nomes NSS:" << nssNs / 1000000.0 << "ms"
                          << "; identidade rápida:" << quickIdentityNs / 1000000.0 << "ms";
    }
}

void ProcessModel::desktopIndexAvailable()
{
    // O worker indexa .desktop fora da UI. Atualizar classificação/nome sem
    // aguardar a resolução custosa dos ícones.
    for (Entry &entry : m_all) {
        const auto cached = m_identityCache.find(entry.pid);
        if (cached == m_identityCache.end() || cached->desktopIndexed) continue;
        entry.identity = m_resolver->quickIdentity(entry.name, entry.arguments);
        cached->value = entry.identity;
        cached->complete = false;
        cached->desktopIndexed = true;
    }
    if (!m_all.isEmpty()) {
        m_windowsCount = 0;
        for (const auto &entry : std::as_const(m_all))
            if (entry.identity.windows) ++m_windowsCount;
        rebuild();
        emit totalCountChanged();
        emit selectedInfoChanged();
        scheduleIdentityUpdates();
    }
}

void ProcessModel::scheduleIdentityUpdates()
{
    // Recriar a fila depois de cada amostragem impede resolver PID reaproveitado.
    m_identityTimer.stop();
    m_identityQueue.clear();
    QVector<int> background;
    for (int i = 0; i < m_all.size(); ++i) {
        const Entry &entry = m_all.at(i);
        const auto cached = m_identityCache.constFind(entry.pid);
        if (cached == m_identityCache.cend() || cached->complete) continue;
        if (entry.identity.desktopMatched || entry.identity.windows)
            m_identityQueue.append(i); // Aplicativos e Wine/Proton primeiro.
        else
            background.append(i);
    }
    m_identityQueue += background;
    m_identityPosition = 0;
    if (!m_identityElapsed.isValid() && !m_identityQueue.isEmpty()) m_identityElapsed.start();
    if (!m_identityQueue.isEmpty()) m_identityTimer.start(16);
}

void ProcessModel::resolveIdentityBatch()
{
    QElapsedTimer batchClock;
    batchClock.start();
    int processed = 0;
    bool structureChanged = false;
    QHash<int, AppIdentity> iconOnlyChanges;
    while (m_identityPosition < m_identityQueue.size()) {
        const int pos = m_identityQueue.at(m_identityPosition++);
        if (pos < 0 || pos >= m_all.size()) continue;
        Entry &entry = m_all[pos];
        auto cached = m_identityCache.find(entry.pid);
        if (cached == m_identityCache.end() || cached->complete ||
            cached->startTime != entry.startTime ||
            cached->signature != entry.identitySignature) continue;
        // Confirmar startTime antes de ler /proc/PID/exe e recursos externos.
        if (processStartTime(entry.pid) != entry.startTime) continue;
        const QString exe = QFileInfo(QStringLiteral("/proc/%1/exe").arg(entry.pid)).symLinkTarget();
        AppIdentity identity = m_resolver->resolve(entry.pid, entry.name,
                                                   entry.arguments, exe);
        const AppIdentity old = entry.identity;
        const bool structural = old.windows != identity.windows ||
            old.desktopMatched != identity.desktopMatched ||
            old.desktopFile != identity.desktopFile ||
            old.displayName != identity.displayName ||
            old.platform != identity.platform;
        entry.identity = std::move(identity);
        cached->value = entry.identity;
        cached->complete = true;
        cached->desktopIndexed = m_resolver->isDesktopIndexReady();
        m_windowsCount += int(entry.identity.windows) - int(old.windows);
        structureChanged = structureChanged || structural;
        if (!structural) iconOnlyChanges.insert(entry.pid, entry.identity);
        ++processed;
        // Nunca realizar centenas de QIcon::pixmap/PE em um único frame.
        if (processed >= 12 || batchClock.elapsed() >= 7) break;
    }
    if (processed) {
        if (structureChanged) {
            // Só reordenar categorias/linhas quando uma identidade realmente mudou.
            rebuild();
            emit totalCountChanged();
        } else {
            // A maioria dos lotes só troca o ícone genérico pelo real.
            // Não reorganizar a ListView nem alterar a posição da rolagem.
            for (int i = 0; i < m_rows.size(); ++i) {
                Row &row = m_rows[i];
                if (row.header || !iconOnlyChanges.contains(row.entry.pid)) continue;
                row.entry.identity = iconOnlyChanges.value(row.entry.pid);
                emit dataChanged(index(i, 0), index(i, 0),
                    {DisplayNameRole, IconUrlRole, IconSourceRole, ExePathRole, DesktopFileRole});
            }
        }
        emit selectedInfoChanged();
    }
    if (m_identityPosition < m_identityQueue.size()) {
        m_identityTimer.start(16);
    } else if (qEnvironmentVariableIsSet("TAREFAS11_STARTUP_TRACE")) {
        qInfo().noquote() << "[Tarefas11 startup] identificação incremental concluída:"
                          << m_identityElapsed.elapsed() << "ms desde a primeira amostragem";
    }
}

void ProcessModel::rebuild()
{
    auto typeMatches = [&](const Entry &entry) {
        const QString category = classifyCategory(entry);
        if (m_typeFilter == QStringLiteral("all")) return true;
        if (m_typeFilter == QStringLiteral("linux")) return category == QStringLiteral("applications");
        if (m_typeFilter == QStringLiteral("windows") || m_typeFilter == QStringLiteral("exe"))
            return category == QStringLiteral("windows");
        if (m_typeFilter == QStringLiteral("background")) return category == QStringLiteral("background");
        if (m_typeFilter == QStringLiteral("applications")) return category == QStringLiteral("applications");
        return true;
    };
    auto textMatches = [&](const Entry &entry) {
        if (m_filter.isEmpty()) return true;
        return entry.name.contains(m_filter, Qt::CaseInsensitive) ||
            entry.identity.displayName.contains(m_filter, Qt::CaseInsensitive) ||
            entry.identity.platform.contains(m_filter, Qt::CaseInsensitive) ||
            entry.command.contains(m_filter, Qt::CaseInsensitive) ||
            entry.user.contains(m_filter, Qt::CaseInsensitive) ||
            QString::number(entry.pid).contains(m_filter);
    };
    auto compare = [&](const Entry *a, const Entry *b) {
        const QString &field = m_sortField;
        if (field == QStringLiteral("name")) {
            const int c = QString::compare(a->identity.displayName, b->identity.displayName, Qt::CaseInsensitive);
            if (c != 0) return c < 0;
        } else if (field == QStringLiteral("pid")) {
            if (a->pid != b->pid) return a->pid < b->pid;
        } else if (field == QStringLiteral("memory")) {
            if (a->memoryMiB != b->memoryMiB) return a->memoryMiB > b->memoryMiB;
        } else if (field == QStringLiteral("swap")) {
            if (a->swapMiB != b->swapMiB) return a->swapMiB > b->swapMiB;
        } else if (field == QStringLiteral("disk")) {
            if (a->diskMiBs != b->diskMiBs) return a->diskMiBs > b->diskMiBs;
        } else if (field == QStringLiteral("ioshare")) {
            if (a->ioSharePercent != b->ioSharePercent) return a->ioSharePercent > b->ioSharePercent;
        } else if (field == QStringLiteral("network")) {
            if (a->networkMiBs != b->networkMiBs) return a->networkMiBs > b->networkMiBs;
        } else if (field == QStringLiteral("read")) {
            if (a->readMiBs != b->readMiBs) return a->readMiBs > b->readMiBs;
        } else if (field == QStringLiteral("write")) {
            if (a->writeMiBs != b->writeMiBs) return a->writeMiBs > b->writeMiBs;
        } else if (field == QStringLiteral("virtual")) {
            if (a->virtualMiB != b->virtualMiB) return a->virtualMiB > b->virtualMiB;
        } else if (field == QStringLiteral("threads")) {
            if (a->threads != b->threads) return a->threads > b->threads;
        } else if (field == QStringLiteral("nice")) {
            if (a->nice != b->nice) return a->nice > b->nice;
        } else if (field == QStringLiteral("ppid")) {
            if (a->ppid != b->ppid) return a->ppid < b->ppid;
        } else if (field == QStringLiteral("platform")) {
            const int c = QString::compare(a->identity.platform, b->identity.platform, Qt::CaseInsensitive);
            if (c != 0) return c < 0;
        } else if (field == QStringLiteral("user")) {
            const int c = QString::compare(a->user, b->user, Qt::CaseInsensitive);
            if (c != 0) return c < 0;
        } else {
            if (a->cpu != b->cpu) return a->cpu > b->cpu;
        }
        return a->pid < b->pid;
    };

    QVector<Row> nextRows;
    int visibleProcesses = 0;
    int visibleApplicationRoots = 0;

    if (!m_groupMode) {
        QVector<const Entry *> result;
        result.reserve(m_all.size());
        for (const auto &entry : m_all) {
            if (!typeMatches(entry) || !textMatches(entry)) continue;
            result.append(&entry);
        }
        std::sort(result.begin(), result.end(), compare);
        nextRows.reserve(result.size());
        for (const Entry *entry : result) {
            Row row;
            row.entry = *entry;
            row.ownMemoryMiB = entry->memoryMiB;
            row.categoryKey = classifyCategory(*entry);
            row.categoryTitle = categoryTitle(row.categoryKey);
            nextRows.append(std::move(row));
        }
        visibleProcesses = result.size();
    } else {
        // Classificação de contexto: subprocessos sem .desktop herdam o aplicativo
        // pai, preservando Firefox/Chromium mesmo se seus workers tiverem outro comm.
        const QStringList categoryOrder = {
            QStringLiteral("applications"), QStringLiteral("windows"), QStringLiteral("background")
        };
        QHash<int, const Entry *> allByPid;
        for (const Entry &entry : m_all) allByPid.insert(entry.pid, &entry);
        QHash<int, QString> categoryByPid;
        QSet<int> resolving;
        std::function<QString(const Entry *)> resolveCategory = [&](const Entry *entry) -> QString {
            if (!entry) return QStringLiteral("background");
            if (categoryByPid.contains(entry->pid)) return categoryByPid.value(entry->pid);
            if (resolving.contains(entry->pid)) return QStringLiteral("background");
            resolving.insert(entry->pid);
            QString result = classifyCategory(*entry);
            if (result == QStringLiteral("background")) {
                const Entry *parent = allByPid.value(entry->ppid, nullptr);
                if (parent && parent != entry && parent->user == entry->user &&
                    parent->pid != 1) result = resolveCategory(parent);
            }
            resolving.remove(entry->pid);
            categoryByPid.insert(entry->pid, result);
            return result;
        };
        for (const Entry &entry : m_all) resolveCategory(&entry);

        auto categoryMatchesFilter = [&](const QString &category) {
            if (m_typeFilter == QStringLiteral("all")) return true;
            if (m_typeFilter == QStringLiteral("exe") || m_typeFilter == QStringLiteral("windows"))
                return category == QStringLiteral("windows");
            if (m_typeFilter == QStringLiteral("linux") || m_typeFilter == QStringLiteral("applications"))
                return category == QStringLiteral("applications");
            return category == QStringLiteral("background");
        };
        struct GroupTotals {
            int count = 0;
            double cpu = 0, memory = 0, swap = 0, disk = 0, ioShare = 0;
            double read = 0, write = 0, network = 0, networkRx = 0, networkTx = 0;
            bool ioAvailable = false, networkAvailable = false, swapAvailable = false;
        };
        struct CategoryData {
            QVector<const Entry *> entries;
            QHash<int, const Entry *> byPid;
            QHash<int, QVector<const Entry *>> children;
            QHash<int, bool> visibleMap;
            QHash<int, GroupTotals> totals;
        };
        QHash<QString, CategoryData> categories;
        for (const Entry &entry : m_all) {
            const QString cat = categoryByPid.value(entry.pid, QStringLiteral("background"));
            if (!categoryMatchesFilter(cat)) continue;
            auto &group = categories[cat];
            group.entries.append(&entry);
            group.byPid.insert(entry.pid, &entry);
        }
        for (auto it = categories.begin(); it != categories.end(); ++it) {
            for (const Entry *entry : std::as_const(it->entries)) {
                const Entry *parent = it->byPid.value(entry->ppid, nullptr);
                if (!parent || parent == entry) continue;
                // Aplicativos identificados por arquivos .desktop diferentes não
                // viram um único aplicativo só porque um iniciou o outro.
                if (entry->identity.desktopMatched && parent->identity.desktopMatched &&
                    !entry->identity.desktopFile.isEmpty() && !parent->identity.desktopFile.isEmpty() &&
                    entry->identity.desktopFile != parent->identity.desktopFile) continue;
                it->children[parent->pid].append(entry);
            }
            for (auto child = it->children.begin(); child != it->children.end(); ++child)
                std::sort(child->begin(), child->end(), compare);
            QSet<int> active;
            std::function<bool(int)> shouldShow = [&](int pid) -> bool {
                if (it->visibleMap.contains(pid)) return it->visibleMap.value(pid);
                if (active.contains(pid)) return false;
                const Entry *entry = it->byPid.value(pid, nullptr);
                if (!entry) return false;
                active.insert(pid);
                bool visible = textMatches(*entry);
                for (const Entry *child : it->children.value(pid)) {
                    if (shouldShow(child->pid)) visible = true;
                }
                active.remove(pid);
                it->visibleMap.insert(pid, visible);
                return visible;
            };
            for (const Entry *entry : std::as_const(it->entries)) shouldShow(entry->pid);

            // Somar pai + TODOS os descendentes, inclusive os recolhidos na UI.
            // A soma RSS é uma estimativa: páginas compartilhadas podem ser
            // contadas em mais de um PID. Detalhes permanece por PID individual.
            QSet<int> calculating;
            std::function<GroupTotals(const Entry *)> aggregate = [&](const Entry *entry) -> GroupTotals {
                if (it->totals.contains(entry->pid)) return it->totals.value(entry->pid);
                if (calculating.contains(entry->pid)) return {};
                calculating.insert(entry->pid);
                GroupTotals sum;
                sum.count = 1;
                sum.cpu = entry->cpu;
                sum.memory = entry->memoryMiB;
                sum.swap = entry->swapMiB;
                sum.swapAvailable = entry->swapAvailable;
                sum.disk = entry->diskMiBs;
                sum.ioShare = entry->ioSharePercent;
                sum.read = entry->readMiBs;
                sum.write = entry->writeMiBs;
                sum.network = entry->networkMiBs;
                sum.networkRx = entry->networkRxMiBs;
                sum.networkTx = entry->networkTxMiBs;
                sum.ioAvailable = entry->ioAvailable;
                sum.networkAvailable = entry->networkAvailable;
                for (const Entry *child : it->children.value(entry->pid)) {
                    if (child->pid == entry->pid || calculating.contains(child->pid)) continue;
                    const GroupTotals sub = aggregate(child);
                    sum.count += sub.count;
                    sum.cpu += sub.cpu;
                    sum.memory += sub.memory;
                    sum.swap += sub.swap;
                    sum.swapAvailable = sum.swapAvailable && sub.swapAvailable;
                    sum.disk += sub.disk;
                    sum.ioShare += sub.ioShare;
                    sum.read += sub.read;
                    sum.write += sub.write;
                    sum.network += sub.network;
                    sum.networkRx += sub.networkRx;
                    sum.networkTx += sub.networkTx;
                    sum.ioAvailable = sum.ioAvailable || sub.ioAvailable;
                    sum.networkAvailable = sum.networkAvailable || sub.networkAvailable;
                }
                calculating.remove(entry->pid);
                it->totals.insert(entry->pid, sum);
                return sum;
            };
            for (const Entry *entry : std::as_const(it->entries)) aggregate(entry);
            // Quando se ordena uma coluna de consumo, usar o total exibido
            // nas linhas agrupadas, não só o consumo individual do pai.
            auto groupedLess = [&](const Entry *a, const Entry *b) {
                const GroupTotals aa = it->totals.value(a->pid);
                const GroupTotals bb = it->totals.value(b->pid);
                const QString field = m_sortField;
                if (field == QStringLiteral("memory") && aa.memory != bb.memory) return aa.memory > bb.memory;
                if (field == QStringLiteral("swap") && aa.swap != bb.swap) return aa.swap > bb.swap;
                if (field == QStringLiteral("cpu") && aa.cpu != bb.cpu) return aa.cpu > bb.cpu;
                if (field == QStringLiteral("disk") && aa.disk != bb.disk) return aa.disk > bb.disk;
                if (field == QStringLiteral("ioshare") && aa.ioShare != bb.ioShare) return aa.ioShare > bb.ioShare;
                if (field == QStringLiteral("network") && aa.network != bb.network) return aa.network > bb.network;
                if (field == QStringLiteral("read") && aa.read != bb.read) return aa.read > bb.read;
                if (field == QStringLiteral("write") && aa.write != bb.write) return aa.write > bb.write;
                return compare(a, b);
            };
            for (auto ci = it->children.begin(); ci != it->children.end(); ++ci)
                std::sort(ci->begin(), ci->end(), groupedLess);
        }

        for (const QString &category : categoryOrder) {
            auto it = categories.find(category);
            if (it == categories.end()) continue;
            QVector<const Entry *> roots;
            QSet<int> attachedChildren;
            for (auto ci = it->children.cbegin(); ci != it->children.cend(); ++ci)
                for (const Entry *child : ci.value()) attachedChildren.insert(child->pid);
            int matchedCount = 0;
            for (const Entry *entry : std::as_const(it->entries)) {
                if (!it->visibleMap.value(entry->pid)) continue;
                ++matchedCount;
                if (!attachedChildren.contains(entry->pid)) roots.append(entry);
            }
            if (!matchedCount) continue;
            std::sort(roots.begin(), roots.end(), [&](const Entry *a, const Entry *b) {
                const GroupTotals aa = it->totals.value(a->pid);
                const GroupTotals bb = it->totals.value(b->pid);
                if (m_sortField == QStringLiteral("memory") && aa.memory != bb.memory) return aa.memory > bb.memory;
                if (m_sortField == QStringLiteral("swap") && aa.swap != bb.swap) return aa.swap > bb.swap;
                if (m_sortField == QStringLiteral("cpu") && aa.cpu != bb.cpu) return aa.cpu > bb.cpu;
                if (m_sortField == QStringLiteral("disk") && aa.disk != bb.disk) return aa.disk > bb.disk;
                return compare(a, b);
            });
            if (category == QStringLiteral("applications")) visibleApplicationRoots = roots.size();

            Row header;
            header.header = true;
            header.categoryKey = category;
            // Contagem de aplicativos abertos = linhas principais, não seus processos auxiliares.
            const int categoryCount = category == QStringLiteral("applications") ? roots.size() : matchedCount;
            header.categoryTitle = categoryTitle(category) + QStringLiteral(" (%1)").arg(categoryCount);
            header.categoryCount = categoryCount;
            header.expandable = true;
            header.expanded = !m_collapsedCategories.contains(category);
            header.childCount = matchedCount;
            const bool categoryExpanded = header.expanded;
            nextRows.append(std::move(header));
            if (!categoryExpanded) continue;

            QSet<int> appended;
            std::function<void(const Entry*, int)> addRow = [&](const Entry *entry, int level) {
                if (level > 32 || appended.contains(entry->pid)) return;
                appended.insert(entry->pid);
                QVector<const Entry *> children;
                for (const Entry *child : it->children.value(entry->pid))
                    if (it->visibleMap.value(child->pid) && !appended.contains(child->pid)) children.append(child);
                Row row;
                row.entry = *entry;
                row.ownMemoryMiB = entry->memoryMiB;
                const GroupTotals totals = it->totals.value(entry->pid);
                row.groupProcessCount = std::max(1, totals.count);
                row.aggregated = row.groupProcessCount > 1;
                if (row.aggregated) {
                    row.entry.cpu = std::clamp(totals.cpu, 0.0, 100.0);
                    row.entry.memoryMiB = totals.memory;
                    row.entry.swapMiB = totals.swap;
                    row.entry.swapAvailable = totals.swapAvailable;
                    row.entry.diskMiBs = totals.disk;
                    row.entry.ioSharePercent = std::clamp(totals.ioShare, 0.0, 100.0);
                    row.entry.readMiBs = totals.read;
                    row.entry.writeMiBs = totals.write;
                    row.entry.networkMiBs = totals.network;
                    row.entry.networkRxMiBs = totals.networkRx;
                    row.entry.networkTxMiBs = totals.networkTx;
                    row.entry.ioAvailable = totals.ioAvailable;
                    row.entry.networkAvailable = totals.networkAvailable;
                }
                row.categoryKey = category;
                row.categoryTitle = categoryTitle(category);
                row.level = level;
                row.expandable = !children.isEmpty();
                row.expanded = row.expandable && (m_expanded.contains(processKey(entry->pid, entry->startTime)) || !m_filter.isEmpty());
                row.childCount = row.aggregated ? row.groupProcessCount - 1 : children.size();
                const bool expanded = row.expanded;
                nextRows.append(std::move(row));
                ++visibleProcesses;
                if (expanded)
                    for (const Entry *child : std::as_const(children)) addRow(child, level + 1);
            };
            for (const Entry *entry : std::as_const(roots)) addRow(entry, 0);
            // Fallback para ciclos inesperados na árvore de processos.
            for (const Entry *entry : std::as_const(it->entries)) {
                if (it->visibleMap.value(entry->pid) && !attachedChildren.contains(entry->pid)) {
                    // Normalmente já está em roots.
                    if (!appended.contains(entry->pid)) addRow(entry, 0);
                }
            }
        }
    }

    // Atualizar a árvore incrementalmente. Resetar a cada criação/saída de PID fazia
    // ListView perder a posição da barra, sobretudo em Detalhes.
    auto keyFor = [this](const Row &row) -> QString {
        return row.header ? QStringLiteral("category:") + row.categoryKey :
            QStringLiteral("pid:") + processKey(row.entry.pid, row.entry.startTime);
    };
    QSet<QString> desiredKeys;
    for (const Row &row : std::as_const(nextRows)) desiredKeys.insert(keyFor(row));
    // Excluir em ordem reversa para preservar índices dos demais itens.
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        if (desiredKeys.contains(keyFor(m_rows.at(i)))) continue;
        beginRemoveRows(QModelIndex(), i, i);
        m_rows.removeAt(i);
        endRemoveRows();
    }
    for (int i = 0; i < nextRows.size(); ++i) {
        const QString wanted = keyFor(nextRows.at(i));
        if (i < m_rows.size() && keyFor(m_rows.at(i)) == wanted) {
            m_rows[i] = std::move(nextRows[i]);
            emit dataChanged(index(i, 0), index(i, 0));
            continue;
        }
        int existing = -1;
        for (int j = i + 1; j < m_rows.size(); ++j) {
            if (keyFor(m_rows.at(j)) == wanted) { existing = j; break; }
        }
        if (existing >= 0) {
            beginMoveRows(QModelIndex(), existing, existing, QModelIndex(), i);
            m_rows.move(existing, i);
            endMoveRows();
            m_rows[i] = std::move(nextRows[i]);
            emit dataChanged(index(i, 0), index(i, 0));
        } else {
            beginInsertRows(QModelIndex(), i, i);
            m_rows.insert(i, std::move(nextRows[i]));
            endInsertRows();
        }
    }
    while (m_rows.size() > nextRows.size()) {
        const int i = m_rows.size() - 1;
        beginRemoveRows(QModelIndex(), i, i);
        m_rows.removeAt(i);
        endRemoveRows();
    }
    const int previousVisible = m_visibleProcessCount;
    const int previousApps = m_appsCount;
    m_visibleProcessCount = visibleProcesses;
    if (m_groupMode) m_appsCount = visibleApplicationRoots;
    if (m_visibleProcessCount != previousVisible || previousApps != m_appsCount) emit totalCountChanged();
}

bool ProcessModel::validateProcess(int pid, quint64 startTime)
{
    if (pid <= 1 || pid == static_cast<int>(::getpid())) {
        emit actionError(QStringLiteral("Este processo é protegido contra encerramento por este aplicativo."));
        return false;
    }
    if (startTime == 0 || processStartTime(pid) != startTime) {
        emit actionError(QStringLiteral("O processo não existe mais ou o PID foi reutilizado."));
        return false;
    }
    return true;
}

bool ProcessModel::terminate(int pid, qulonglong startTime, bool force)
{
    if (!validateProcess(pid, startTime)) return false;
    if (!signalProcess(pid, force ? SIGKILL : SIGTERM)) {
        emit actionError(QStringLiteral("Não foi possível finalizar PID %1: %2")
                         .arg(pid).arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    emit actionSuccess(force ? QStringLiteral("SIGKILL enviado ao processo %1").arg(pid)
                             : QStringLiteral("SIGTERM enviado ao processo %1").arg(pid));
    return true;
}

bool ProcessModel::reducePriority(int pid, qulonglong startTime)
{
    if (!validateProcess(pid, startTime)) return false;
    errno = 0;
    const int oldNice = ::getpriority(PRIO_PROCESS, static_cast<id_t>(pid));
    if (errno != 0) {
        emit actionError(QStringLiteral("Não foi possível consultar a prioridade do processo."));
        return false;
    }
    if (oldNice < 10 && ::setpriority(PRIO_PROCESS, static_cast<id_t>(pid), 10) != 0) {
        emit actionError(QStringLiteral("Não foi possível reduzir a prioridade: %1")
                         .arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    emit actionSuccess(QStringLiteral("Prioridade de CPU do processo %1 reduzida (nice %2).")
                       .arg(pid).arg(std::max(10, oldNice)));
    return true;
}

bool ProcessModel::launchCommand(const QString &command)
{
    const auto parts = QProcess::splitCommand(command.trimmed());
    if (parts.isEmpty()) {
        emit actionError(QStringLiteral("Informe um executável."));
        return false;
    }
    const QString program = parts.first();
    const QStringList args = parts.mid(1);
    if (!QProcess::startDetached(program, args)) {
        emit actionError(QStringLiteral("Não foi possível executar: %1").arg(program));
        return false;
    }
    emit actionSuccess(QStringLiteral("Nova tarefa iniciada: %1").arg(program));
    return true;
}

QString ProcessModel::formatMemoryMiB(double valueMiB) const
{
    return MemoryMetrics::formatMiB(valueMiB);
}

void ProcessModel::copyPid(int pid)
{
    QGuiApplication::clipboard()->setText(QString::number(pid));
    emit actionSuccess(QStringLiteral("PID %1 copiado para a área de transferência.").arg(pid));
}

const ProcessModel::Entry *ProcessModel::selectedEntry() const
{
    for (const auto &p : m_all) {
        if (p.pid == m_selectedPid && p.startTime == m_selectedStart) return &p;
    }
    return nullptr;
}

void ProcessModel::selectProcess(int pid, qulonglong startTime)
{
    if (pid == m_selectedPid && startTime == m_selectedStart) return;
    m_selectedPid = pid;
    m_selectedStart = startTime;
    emit selectedInfoChanged();
}

void ProcessModel::toggleExpanded(int pid, qulonglong startTime)
{
    if (pid <= 0 || startTime == 0) return;
    const QString key = processKey(pid, startTime);
    if (m_expanded.contains(key)) m_expanded.remove(key);
    else m_expanded.insert(key);
    rebuild();
}

bool ProcessModel::isExpanded(int pid, qulonglong startTime) const
{
    return m_expanded.contains(processKey(pid, startTime));
}

void ProcessModel::toggleCategory(const QString &categoryKey)
{
    if (!m_groupMode || (categoryKey != QStringLiteral("applications") &&
                         categoryKey != QStringLiteral("windows") &&
                         categoryKey != QStringLiteral("background"))) return;
    if (m_collapsedCategories.contains(categoryKey)) m_collapsedCategories.remove(categoryKey);
    else m_collapsedCategories.insert(categoryKey);
    rebuild();
}

bool ProcessModel::isCategoryCollapsed(const QString &categoryKey) const
{
    return m_collapsedCategories.contains(categoryKey);
}

QString ProcessModel::rowKeyAt(int row) const
{
    if (row < 0 || row >= m_rows.size()) return {};
    const Row &r = m_rows.at(row);
    return r.header ? QStringLiteral("category:") + r.categoryKey :
                      QStringLiteral("pid:") + processKey(r.entry.pid, r.entry.startTime);
}

int ProcessModel::indexOfRow(const QString &key) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (rowKeyAt(i) == key) return i;
    }
    return -1;
}

QVariantMap ProcessModel::selectedInfo() const
{
    const Entry *p = selectedEntry();
    if (!p) return {};
    QVariantMap info = {
        {QStringLiteral("pid"), p->pid}, {QStringLiteral("ppid"), p->ppid},
        {QStringLiteral("name"), p->name}, {QStringLiteral("displayName"), p->identity.displayName},
        {QStringLiteral("platform"), p->identity.platform}, {QStringLiteral("iconUrl"), p->identity.iconUrl},
        {QStringLiteral("iconSource"), p->identity.iconSource},
        {QStringLiteral("exePath"), p->identity.executablePath},
        {QStringLiteral("desktopFile"), p->identity.desktopFile},
        {QStringLiteral("command"), p->command},
        {QStringLiteral("user"), p->user}, {QStringLiteral("state"), p->state},
        {QStringLiteral("cpu"), p->cpu}, {QStringLiteral("memoryMiB"), p->memoryMiB},
        {QStringLiteral("swapMiB"), p->swapAvailable ? QVariant(p->swapMiB) : QVariant()},
        {QStringLiteral("swapAvailable"), p->swapAvailable},
        {QStringLiteral("virtualMiB"), p->virtualMiB},
        {QStringLiteral("sharedMiB"), p->sharedMiB},
        {QStringLiteral("threads"), p->threads}, {QStringLiteral("nice"), p->nice},
        {QStringLiteral("readMiBs"), p->ioAvailable ? QVariant(p->readMiBs) : QVariant()},
        {QStringLiteral("writeMiBs"), p->ioAvailable ? QVariant(p->writeMiBs) : QVariant()},
        {QStringLiteral("cpuTimeSeconds"), p->cpuTimeSeconds},
        {QStringLiteral("ioSharePercent"), p->ioSharePercent},
        {QStringLiteral("networkMiBs"), p->networkAvailable ? QVariant(p->networkMiBs) : QVariant()},
        {QStringLiteral("networkRxMiBs"), p->networkAvailable ? QVariant(p->networkRxMiBs) : QVariant()},
        {QStringLiteral("networkTxMiBs"), p->networkAvailable ? QVariant(p->networkTxMiBs) : QVariant()}
    };
    for (const Row &row : m_rows) {
        if (!row.header && row.entry.pid == p->pid && row.entry.startTime == p->startTime) {
            info.insert(QStringLiteral("groupMemoryMiB"), row.entry.memoryMiB);
            info.insert(QStringLiteral("groupSwapMiB"), row.entry.swapAvailable ? QVariant(row.entry.swapMiB) : QVariant());
            info.insert(QStringLiteral("groupSwapAvailable"), row.entry.swapAvailable);
            info.insert(QStringLiteral("groupProcessCount"), row.groupProcessCount);
            info.insert(QStringLiteral("groupAggregated"), row.aggregated);
            break;
        }
    }
    return info;
}

void ProcessModel::copyCommand(int pid, qulonglong startTime)
{
    const Entry *p = selectedEntry();
    if (!p || p->pid != pid || p->startTime != startTime) {
        emit actionError(QStringLiteral("Seleção expirou: o processo terminou ou mudou."));
        return;
    }
    QGuiApplication::clipboard()->setText(p->command);
    emit actionSuccess(QStringLiteral("Linha de comando copiada."));
}

void ProcessModel::openContainingFolder(int pid, qulonglong startTime)
{
    const Entry *p = selectedEntry();
    if (!p || p->pid != pid || p->startTime != startTime) {
        emit actionError(QStringLiteral("Seleção expirou: o processo terminou ou mudou."));
        return;
    }
    QString target = p->identity.executablePath;
    if (target.isEmpty()) target = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();
    if (target.isEmpty() || !QFileInfo::exists(target)) {
        emit actionError(QStringLiteral("O caminho do executável não está acessível."));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(target).absolutePath())))
        emit actionError(QStringLiteral("Não foi possível abrir a pasta no gerenciador de arquivos."));
}
