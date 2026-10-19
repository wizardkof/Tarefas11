#include "AppIdentity.h"
#include "PeIconExtractor.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QProcess>
#include <QReadLocker>
#include <QSet>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QWriteLocker>
#include <QDebug>
#include <QtConcurrent/QtConcurrentRun>
#include <utility>

#include <algorithm>

namespace {
QString base(const QString &path)
{
    const QString clean = path.trimmed().remove('"').replace('\\', '/');
    return QFileInfo(clean).fileName().toLower();
}
QString keyOf(const QString &name)
{
    return name.trimmed().toCaseFolded();
}
QString executableCandidate(const QString &arg)
{
    QString candidate = arg.trimmed();
    if (candidate.startsWith('"') && candidate.endsWith('"')) candidate = candidate.mid(1, candidate.size() - 2);
    return candidate;
}
bool looksLikeExe(const QString &arg)
{
    return executableCandidate(arg).endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive);
}

QString readProcessEnvironment(int pid, const QByteArray &name)
{
    QFile file(QStringLiteral("/proc/%1/environ").arg(pid));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray contents = file.read(512 * 1024);
    const QByteArray prefix = name + '=';
    for (const auto &line : contents.split('\0')) {
        if (line.startsWith(prefix)) return QString::fromLocal8Bit(line.mid(prefix.size()));
    }
    return {};
}

QString casePreservingPath(const QString &name)
{
    QFileInfo initial(name);
    if (initial.isFile()) return initial.canonicalFilePath();
    // Wine/Windows ignoram maiúsculas/minúsculas; ext4/Btrfs normalmente não.
    if (!name.startsWith('/')) return {};
    const QStringList parts = QDir::cleanPath(name).split('/', Qt::SkipEmptyParts);
    if (parts.size() > 30) return {};
    QString current = QStringLiteral("/");
    for (const QString &part : parts) {
        if (part == QStringLiteral("..") || part == QStringLiteral(".")) return {};
        const QString direct = QDir(current).filePath(part);
        if (QFileInfo::exists(direct)) { current = direct; continue; }
        QDir folder(current);
        // Não percorrer árvores amplas do host; só resolver o diretório corrente.
        const QStringList entries = folder.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System);
        if (entries.size() > 2000) return {};
        auto found = std::find_if(entries.cbegin(), entries.cend(), [&](const QString &f) {
            return f.compare(part, Qt::CaseInsensitive) == 0;
        });
        if (found == entries.cend()) return {};
        current = folder.filePath(*found);
    }
    QFileInfo result(current);
    return result.isFile() ? result.canonicalFilePath() : QString{};
}

QString winePath(int pid, const QString &candidate)
{
    QString path = executableCandidate(candidate);
    if (path.startsWith(QStringLiteral("\\\\?\\")) || path.startsWith(QStringLiteral("\\??\\")))
        path = path.mid(4);
    if (path.startsWith('/') && path.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
        return casePreservingPath(path);
    if (path.size() < 4 || path[1] != ':' || (path[2] != '\\' && path[2] != '/')) return {};
    const QChar drive = path[0].toLower();
    const QString tail = path.mid(3).replace('\\', '/');
    if (tail.split('/').contains(QStringLiteral(".."))) return {}; // proteção de path
    if (drive == 'z') return casePreservingPath(QStringLiteral("/") + tail);
    QString prefix = readProcessEnvironment(pid, "WINEPREFIX");
    if (prefix.isEmpty()) {
        const QString steamCompat = readProcessEnvironment(pid, "STEAM_COMPAT_DATA_PATH");
        prefix = steamCompat.isEmpty() ? QDir::homePath() + QStringLiteral("/.wine")
                                       : steamCompat + QStringLiteral("/pfx");
    }
    if (!prefix.startsWith('/')) return {};
    const QString dos = prefix + QStringLiteral("/dosdevices/") + drive + ':';
    const QString root = QFileInfo::exists(dos) ? dos :
                         drive == 'c' ? prefix + QStringLiteral("/drive_c") : QString{};
    if (root.isEmpty()) return {};
    return casePreservingPath(root + '/' + tail);
}

QHash<QString, QString> desktopFields(const QString &path)
{
    QFile file(path);
    QHash<QString, QString> values;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024) return values;
    const auto contents = QString::fromUtf8(file.readAll());
    bool active = false;
    for (const auto &line : contents.split('\n')) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith('[')) {
            active = trimmed == QStringLiteral("[Desktop Entry]");
            continue;
        }
        if (!active || trimmed.startsWith('#')) continue;
        const auto eq = trimmed.indexOf('=');
        if (eq > 0) values.insert(trimmed.left(eq), trimmed.mid(eq + 1));
    }
    return values;
}
}

AppIdentityResolver::AppIdentityResolver()
{
    loadDesktopEntries();
}

void AppIdentityResolver::setIndexReadyCallback(std::function<void()> callback)
{
    m_indexReadyCallback = std::move(callback);
    if (m_desktopIndexReady && m_indexReadyCallback) m_indexReadyCallback();
}

void AppIdentityResolver::loadDesktopEntries()
{
    // Inclui os temas exportados pelo Flatpak quando não estão em XDG_DATA_DIRS.
    auto themePaths = QIcon::themeSearchPaths();
    for (const QString &extra : {
            QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/icons"),
            QStringLiteral("/var/lib/flatpak/exports/share/icons")}) {
        if (QDir(extra).exists() && !themePaths.contains(extra)) themePaths.append(extra);
    }
    QIcon::setThemeSearchPaths(themePaths);
    QStringList dirs;
    const auto xdgDirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const auto &root : xdgDirs) dirs.append(root + QStringLiteral("/applications"));
    const QString home = QDir::homePath();
    dirs.append(home + QStringLiteral("/.local/share/flatpak/exports/share/applications"));
    dirs.append(QStringLiteral("/var/lib/flatpak/exports/share/applications"));
    dirs.removeDuplicates();

    // Nenhum QIcon/QPixmap é criado no worker. Somente I/O de .desktop e
    // processamento de texto QtCore ficam fora da thread da interface.
    QObject::connect(&m_desktopWatcher, &QFutureWatcher<DesktopIndex>::finished,
            &m_desktopWatcher, [this]() {
        m_desktopByKey = m_desktopWatcher.result();
        m_desktopIndexReady = true;
        if (qEnvironmentVariableIsSet("TAREFAS11_STARTUP_TRACE"))
            qInfo().noquote() << "[Tarefas11 startup] índice .desktop:"
                              << m_indexElapsed.elapsed() << "ms,"
                              << m_desktopByKey.size() << "chaves";
        if (m_indexReadyCallback) m_indexReadyCallback();
    });
    m_indexElapsed.start();
    m_desktopWatcher.setFuture(QtConcurrent::run([dirs]() -> DesktopIndex {
        DesktopIndex entries;
        const QSet<QString> skip = {"env", "flatpak", "sh", "bash", "python", "python3", "wine", "wine64",
                                    "wine-preloader", "wine64-preloader", "sudo", "systemd", "pkexec"};
        int visited = 0;
        for (const QString &dir : dirs) {
            if (!QDir(dir).exists()) continue;
            QDirIterator it(dir, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext() && ++visited < 15000) {
                const QString path = it.next();
                const auto f = desktopFields(path);
                if (f.value(QStringLiteral("Type")) != QStringLiteral("Application") ||
                    f.value(QStringLiteral("Hidden")) == QStringLiteral("true")) continue;
                Desktop app;
                app.name = f.value(QStringLiteral("Name[pt_BR]"), f.value(QStringLiteral("Name")));
                app.icon = f.value(QStringLiteral("Icon"));
                app.file = path;
                if (app.name.isEmpty() || app.icon.isEmpty()) continue;
                QStringList keys;
                keys << base(path).remove(QStringLiteral(".desktop"));
                keys << base(f.value(QStringLiteral("StartupWMClass")));
                QString command = f.value(QStringLiteral("Exec"));
                auto parts = QProcess::splitCommand(command);
                int cursor = 0;
                if (!parts.isEmpty() && base(parts[0]) == QStringLiteral("env")) {
                    ++cursor;
                    while (cursor < parts.size() && parts[cursor].contains('=') && !parts[cursor].startsWith('/')) ++cursor;
                }
                if (cursor < parts.size() && base(parts[cursor]) == QStringLiteral("flatpak")) {
                    for (int j = cursor + 1; j < parts.size(); ++j) {
                        if (parts[j] == QStringLiteral("run")) {
                            while (++j < parts.size() && parts[j].startsWith('-')) {}
                            if (j < parts.size()) keys << parts[j].toLower();
                            break;
                        }
                    }
                }
                if (cursor < parts.size() && !skip.contains(base(parts[cursor]))) keys << base(parts[cursor]);
                for (const QString &key : keys) {
                    if (!key.isEmpty() && !skip.contains(key) && !entries.contains(keyOf(key)))
                        entries.insert(keyOf(key), app);
                }
            }
        }
        return entries;
    }));
}

QString AppIdentityResolver::findWindowsExecutable(int pid, const QStringList &args,
                                                     const QString &linuxExecutable) const
{
    if (looksLikeExe(linuxExecutable)) {
        const QString native = casePreservingPath(linuxExecutable);
        if (!native.isEmpty()) return native;
    }
    for (int i = 0; i < std::min<qsizetype>(12, args.size()); ++i) {
        if (!looksLikeExe(args[i])) continue;
        const QString found = winePath(pid, args[i]);
        if (!found.isEmpty()) return found;
    }
    // Caminho relativo a /proc/PID/cwd em programas Wine iniciados pelo terminal.
    const QString cwd = QFileInfo(QStringLiteral("/proc/%1/cwd").arg(pid)).symLinkTarget();
    if (!cwd.isEmpty()) {
        for (int i = 0; i < std::min<qsizetype>(8, args.size()); ++i) {
            if (looksLikeExe(args[i]) && !args[i].contains(':')) {
                const QString found = casePreservingPath(QDir(cwd).filePath(QString(args[i]).replace('\\', '/')));
                if (!found.isEmpty()) return found;
            }
        }
    }
    return {};
}

QImage AppIdentityResolver::genericIcon(bool windows)
{
    QImage image(64, 64, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(windows ? QColor("#466d96") : QColor("#576875"));
    p.drawRoundedRect(QRectF(5, 5, 54, 54), 13, 13);
    p.setPen(Qt::white);
    QFont f = p.font(); f.setBold(true); f.setPixelSize(35); p.setFont(f);
    p.drawText(image.rect(), Qt::AlignCenter, windows ? QStringLiteral("W") : QStringLiteral("L"));
    return image;
}

QString AppIdentityResolver::saveIcon(const QString &cacheKey, const QImage &image)
{
    const QString id = QString::fromLatin1(QCryptographicHash::hash(cacheKey.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
    QWriteLocker locker(&m_lock);
    if (!m_images.contains(id)) {
        if (m_images.size() >= 1600) { m_images.clear(); m_iconUrls.clear(); }
        m_images.insert(id, image);
    }
    const QString url = QStringLiteral("image://appicons/") + id;
    m_iconUrls.insert(cacheKey, url);
    return url;
}

QString AppIdentityResolver::icon(const QString &spec, const QString &fallbackKey)
{
    QString unique = QStringLiteral("qt:") + spec;
    if (m_iconUrls.contains(unique)) return m_iconUrls.value(unique);
    QString iconName = spec;
    if (!iconName.startsWith('/') &&
        (iconName.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive) ||
         iconName.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive))) {
        iconName = QFileInfo(iconName).completeBaseName();
    }
    QIcon qicon = spec.startsWith('/') ? QIcon(spec) : QIcon::fromTheme(iconName);
    if (qicon.isNull()) qicon = QIcon::fromTheme(fallbackKey);
    const bool windows = spec == QStringLiteral("application-x-ms-dos-executable");
    QImage image = qicon.isNull() ? genericIcon(windows) : qicon.pixmap(64, 64).toImage();
    if (image.isNull()) image = genericIcon(windows);
    return saveIcon(unique, image);
}

AppIdentity AppIdentityResolver::quickIdentity(const QString &comm, const QStringList &args)
{
    AppIdentity result;
    result.displayName = comm;
    const bool exeName = comm.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive);
    const bool exeArg = !args.isEmpty() && looksLikeExe(args.first());
    const bool wineLaunch = args.size() > 1 &&
        (base(args.first()).startsWith(QStringLiteral("wine")) ||
         base(args.first()).startsWith(QStringLiteral("proton"))) && looksLikeExe(args.at(1));
    result.windows = exeName || exeArg || wineLaunch;
    result.platform = result.windows ? QStringLiteral("Windows (Wine/Proton)") : QStringLiteral("Linux");

    QStringList candidates = {base(args.value(0)), comm.toLower()};
    if (args.size() > 1) candidates << base(args.at(1));
    const QString commandPath = args.value(0).toLower();
    const bool googleChrome = !result.windows &&
        (commandPath.contains(QStringLiteral("/google/chrome/")) ||
         base(args.value(0)).startsWith(QStringLiteral("google-chrome")));
    if (googleChrome) {
        candidates.prepend(QStringLiteral("google-chrome-stable"));
        candidates.prepend(QStringLiteral("google-chrome"));
    }
    for (const QString &candidate : std::as_const(candidates)) {
        auto it = m_desktopByKey.constFind(keyOf(candidate));
        if (it == m_desktopByKey.cend()) continue;
        result.desktopMatched = true;
        result.desktopFile = it->file;
        if (!result.windows) result.displayName = it->name;
        break;
    }
    if (googleChrome && !result.desktopMatched) {
        result.desktopMatched = true;
        result.displayName = QStringLiteral("Google Chrome");
    }
    if (result.windows) {
        QString candidate = exeName ? comm : (exeArg ? args.value(0) : args.value(1));
        candidate.replace('\\', '/');
        const QString exeName = QFileInfo(candidate).completeBaseName();
        if (!exeName.isEmpty()) result.displayName = exeName;
    }
    // Apenas dois ícones genéricos compartilhados; ícones reais são resolvidos
    // em lotes depois que CPU, RAM, SWAP e PIDs já estiverem na tabela.
    result.iconUrl = result.windows
        ? icon(QStringLiteral("application-x-ms-dos-executable"), QStringLiteral("wine"))
        : icon(QStringLiteral("application-x-executable"), QStringLiteral("application-x-executable"));
    result.iconSource = QStringLiteral("Aguardando identificação do ícone");
    return result;
}

AppIdentity AppIdentityResolver::resolve(int pid, const QString &comm,
                                         const QStringList &args, const QString &linuxExecutable)
{
    AppIdentity result;
    result.displayName = comm;
    const bool possiblyWindows = comm.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) ||
        (args.size() > 0 && looksLikeExe(args[0])) ||
        (args.size() > 1 && (base(args[0]).startsWith(QStringLiteral("wine")) ||
                             base(args[0]).startsWith(QStringLiteral("proton"))) && looksLikeExe(args[1]));
    const QString windowsExe = possiblyWindows ? findWindowsExecutable(pid, args, linuxExecutable) : QString{};
    const bool isPE = !windowsExe.isEmpty() && [&] {
        QFile f(windowsExe);
        return f.open(QIODevice::ReadOnly) && f.read(2) == "MZ";
    }();
    result.windows = possiblyWindows && (comm.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) || isPE);
    result.platform = result.windows ? QStringLiteral("Windows (Wine/Proton)") : QStringLiteral("Linux");

    QStringList candidates = {base(linuxExecutable), base(args.value(0)), comm.toLower()};
    if (args.size() > 1) candidates << base(args[1]);
    const QString linuxPath = linuxExecutable.toLower();
    // Chrome usa comm="chrome", mas seu .desktop normalmente se chama google-chrome.desktop.
    // Não confundir /usr/bin/chromium com o navegador proprietário do Google.
    const bool googleChrome = !result.windows &&
        (linuxPath.contains(QStringLiteral("/google/chrome/")) ||
         base(args.value(0)).startsWith(QStringLiteral("google-chrome")));
    if (googleChrome) {
        candidates.prepend(QStringLiteral("google-chrome-stable"));
        candidates.prepend(QStringLiteral("google-chrome"));
    }
    for (const QString &candidate : candidates) {
        const auto it = m_desktopByKey.constFind(keyOf(candidate));
        if (it != m_desktopByKey.constEnd()) {
            result.desktopFile = it->file;
            result.desktopMatched = true;
            if (!result.windows) result.displayName = it->name;
            result.iconSource = QStringLiteral("Ícone .desktop / tema KDE");
            result.iconUrl = icon(it->icon, QStringLiteral("application-x-executable"));
            break;
        }
    }
    if (!result.windows && googleChrome && !result.desktopMatched) {
        result.displayName = QStringLiteral("Google Chrome");
        result.desktopMatched = true; // Identidade pelo diretório do binário, não apenas comm="chrome".
        result.iconUrl = icon(QStringLiteral("google-chrome"), QStringLiteral("web-browser"));
        result.iconSource = QStringLiteral("Identificação por executável Google Chrome / tema de ícones");
    }
    if (result.windows) {
        result.executablePath = isPE ? windowsExe : QString{};
        result.displayName = isPE ? QFileInfo(windowsExe).completeBaseName() : comm;
        if (isPE) {
            const QFileInfo f(windowsExe);
            const QString cacheKey = QStringLiteral("pe:") + windowsExe + ':' +
                QString::number(f.size()) + ':' + QString::number(f.lastModified().toMSecsSinceEpoch());
            if (m_iconUrls.contains(cacheKey)) {
                result.iconUrl = m_iconUrls.value(cacheKey);
                result.iconSource = QStringLiteral("Ícone interno do executável PE");
            } else if (!m_missingPeIcons.contains(cacheKey)) {
                const QImage exeIcon = PeIconExtractor::extract(windowsExe);
                if (!exeIcon.isNull()) {
                    result.iconUrl = saveIcon(cacheKey, exeIcon);
                    result.iconSource = QStringLiteral("Ícone interno do executável PE");
                } else {
                    // Evita reler o mesmo PE sem ícone para cada filho Wine/Proton.
                    if (m_missingPeIcons.size() > 4096) m_missingPeIcons.clear();
                    m_missingPeIcons.insert(cacheKey);
                }
            }
        }
        if (result.iconUrl.isEmpty()) {
            result.iconUrl = icon(QStringLiteral("application-x-ms-dos-executable"), QStringLiteral("wine"));
            result.iconSource = QStringLiteral("Ícone genérico (PE inacessível/sem ícone)");
        }
    } else if (result.iconUrl.isEmpty()) {
        // Threads do kernel não têm /proc/PID/exe. Evitar uma pesquisa
        // cara e inútil no tema para cada nome de worker do kernel.
        const QString basename = linuxExecutable.isEmpty()
            ? QStringLiteral("application-x-executable") : base(linuxExecutable);
        result.iconUrl = icon(basename, QStringLiteral("application-x-executable"));
        result.iconSource = QStringLiteral("Tema de ícones KDE ou ícone genérico");
    }
    return result;
}

QImage AppIdentityResolver::imageForId(const QString &id, const QSize &size) const
{
    QReadLocker locker(&m_lock);
    const QImage image = m_images.value(id);
    if (image.isNull()) return genericIcon(false);
    return size.isValid() ? image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation) : image;
}

QImage AppIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    QImage image = m_resolver->imageForId(id, requestedSize);
    if (size) *size = image.size();
    return image;
}
