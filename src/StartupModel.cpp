#include "StartupModel.h"

#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace {

using Fields = QHash<QString, QString>;

QByteArray readBoundedFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return {};
    return file.readAll();
}

Fields parseDesktop(const QByteArray &contents)
{
    Fields result;
    bool entrySection = false;
    for (const QString &original : QString::fromUtf8(contents).split(QLatin1Char('\n'))) {
        const QString line = original.trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            entrySection = (line == QStringLiteral("[Desktop Entry]"));
            continue;
        }
        if (!entrySection || line.startsWith(QLatin1Char('#'))) continue;
        const int separator = line.indexOf(QLatin1Char('='));
        if (separator > 0) result.insert(line.left(separator).trimmed(), line.mid(separator + 1).trimmed());
    }
    return result;
}

QString localizedName(const Fields &fields, const QString &defaultName)
{
    const QString locale = QLocale::system().name();
    const QString language = locale.section(QLatin1Char('_'), 0, 0);
    const QString name = fields.value(QStringLiteral("Name[") + locale + QLatin1Char(']'),
                         fields.value(QStringLiteral("Name[") + language + QLatin1Char(']'),
                         fields.value(QStringLiteral("Name"), defaultName)));
    return name.isEmpty() ? defaultName : name;
}

bool trueValue(const QString &s)
{
    return s.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0 || s == QStringLiteral("1");
}

bool falseValue(const QString &s)
{
    return s.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0 || s == QStringLiteral("0");
}

bool desktopHasKde(const QString &value)
{
    for (const QString &part : value.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        if (part.compare(QStringLiteral("KDE"), Qt::CaseInsensitive) == 0 ||
            part.compare(QStringLiteral("PLASMA"), Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

bool isApplicable(const Fields &values, QString *reason)
{
    const QString only = values.value(QStringLiteral("OnlyShowIn"));
    const QString notIn = values.value(QStringLiteral("NotShowIn"));
    if (!only.isEmpty() && !desktopHasKde(only)) {
        *reason = QStringLiteral("Destinado a outro ambiente gráfico (OnlyShowIn)");
        return false;
    }
    if (desktopHasKde(notIn)) {
        *reason = QStringLiteral("Excluído do KDE (NotShowIn)");
        return false;
    }
    const QString tryExec = values.value(QStringLiteral("TryExec"));
    if (!tryExec.isEmpty()) {
        const bool exists = tryExec.startsWith(QLatin1Char('/')) ?
                                 (QFileInfo(tryExec).isFile() && QFileInfo(tryExec).isExecutable())
                             : !QStandardPaths::findExecutable(tryExec).isEmpty();
        if (!exists) {
            *reason = QStringLiteral("TryExec indisponível: ") + tryExec;
            return false;
        }
    }
    if (trueValue(values.value(QStringLiteral("X-systemd-skip")))) {
        *reason = QStringLiteral("Ignorado pelo gerador systemd de autostart");
        return false;
    }
    return true;
}

bool rewriteDesktopKey(const QByteArray &contents, const QString &key, const QString &value,
                       QByteArray *output)
{
    const QString source = QString::fromUtf8(contents);
    QStringList lines = source.split(QLatin1Char('\n'));
    bool inSection = false, foundSection = false, updated = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString text = lines[i].trimmed();
        if (text.startsWith(QLatin1Char('[')) && text.endsWith(QLatin1Char(']'))) {
            if (inSection && !updated) {
                lines.insert(i++, key + QLatin1Char('=') + value);
                updated = true;
            }
            inSection = (text == QStringLiteral("[Desktop Entry]"));
            if (inSection) foundSection = true;
        } else if (inSection && text.startsWith(key + QLatin1Char('='))) {
            lines[i] = key + QLatin1Char('=') + value;
            updated = true;
        }
    }
    if (!foundSection) return false;
    if (!updated) lines.append(key + QLatin1Char('=') + value);
    *output = lines.join(QLatin1Char('\n')).toUtf8();
    return true;
}

bool writeAtomically(const QString &path, const QByteArray &data)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    if (f.write(data) != data.size()) { f.cancelWriting(); return false; }
    return f.commit();
}

QString iconUrl(const QString &icon)
{
    return QStringLiteral("image://startupicons/") +
           QString::fromLatin1(QUrl::toPercentEncoding(icon.isEmpty() ? QStringLiteral("application-x-executable") : icon));
}

QStringList configRoots()
{
    const QString homeConfig = qEnvironmentVariable("XDG_CONFIG_HOME");
    const QString local = homeConfig.startsWith(QLatin1Char('/')) ? homeConfig
                          : QDir::homePath() + QStringLiteral("/.config");
    QStringList dirs{local};
    QString system = qEnvironmentVariable("XDG_CONFIG_DIRS", QStringLiteral("/etc/xdg"));
    for (const QString &dir : system.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        if (dir.startsWith(QLatin1Char('/')) && !dirs.contains(dir)) dirs.append(dir);
    }
    return dirs;
}

} // namespace

StartupModel::StartupModel(QObject *parent, bool eagerLoad) : QAbstractListModel(parent)
{
    if (eagerLoad) refresh();
}

int StartupModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visible.size();
}

QVariant StartupModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() < 0 || idx.row() >= m_visible.size()) return {};
    const Entry &e = m_visible.at(idx.row());
    switch (role) {
    case IdRole: return e.id;
    case NameRole: return e.name;
    case IconUrlRole: return iconUrl(e.icon);
    case CommandRole: return e.command;
    case StatusRole: return e.status;
    case SourceRole: return e.source;
    case PathRole: return e.path;
    case KindRole: return e.kind;
    case EnabledRole: return e.enabled;
    case EditableRole: return e.editable;
    case RemovableRole: return e.removable;
    case HeaderRole: return e.header;
    case NotesRole: return e.notes;
    default: return {};
    }
}

QHash<int, QByteArray> StartupModel::roleNames() const
{
    return {{IdRole,"entryId"}, {NameRole,"entryName"}, {IconUrlRole,"entryIcon"},
            {CommandRole,"entryCommand"}, {StatusRole,"entryStatus"},
            {SourceRole,"entrySource"}, {PathRole,"entryPath"},
            {KindRole,"entryKind"}, {EnabledRole,"entryEnabled"},
            {EditableRole,"entryEditable"}, {RemovableRole,"entryRemovable"},
            {HeaderRole,"isHeader"}, {NotesRole,"entryNotes"}};
}

QString StartupModel::userAutostartDir() const
{
    return configRoots().constFirst() + QStringLiteral("/autostart");
}

void StartupModel::setFilterText(const QString &filter)
{
    if (m_filter == filter) return;
    m_filter = filter;
    rebuild();
    emit filterChanged();
}

void StartupModel::refresh()
{
    QVector<Entry> all;
    const QStringList roots = configRoots();
    QHash<QString, QStringList> desktopCandidates;
    for (const QString &root : roots) {
        QDir dir(root + QStringLiteral("/autostart"));
        for (const QFileInfo &f : dir.entryInfoList({QStringLiteral("*.desktop")},
                                                     QDir::Files | QDir::Readable | QDir::NoDotAndDotDot)) {
            desktopCandidates[f.fileName()].append(f.absoluteFilePath());
        }
    }
    for (auto it = desktopCandidates.cbegin(); it != desktopCandidates.cend(); ++it) {
        if (it->isEmpty()) continue;
        const QString first = it->constFirst();
        const Fields data = parseDesktop(readBoundedFile(first));
        if (data.value(QStringLiteral("Type"), QStringLiteral("Application")) != QStringLiteral("Application")) continue;
        Fields labels = data;
        if (it->size() > 1) {
            const Fields lower = parseDesktop(readBoundedFile(it->at(1)));
            for (const QString &key : {QStringLiteral("Name"), QStringLiteral("Name[pt_BR]"),
                                       QStringLiteral("Icon"), QStringLiteral("Exec"),
                                       QStringLiteral("X-KDE-AutostartScript")})
                if (!labels.contains(key)) labels.insert(key, lower.value(key));
        }
        Entry e;
        e.id = QStringLiteral("desktop:") + it.key();
        e.kind = trueValue(labels.value(QStringLiteral("X-KDE-AutostartScript")))
                     ? QStringLiteral("login") : QStringLiteral("application");
        e.name = localizedName(labels, QFileInfo(first).completeBaseName());
        e.icon = labels.value(QStringLiteral("Icon"), QStringLiteral("application-x-executable"));
        e.command = labels.value(QStringLiteral("Exec"));
        e.path = first;
        e.source = first.startsWith(userAutostartDir() + QLatin1Char('/')) ? QStringLiteral("Usuário") : QStringLiteral("Sistema");
        e.editable = true;
        e.hasSystemFallback = it->size() > 1;
        e.removable = e.source == QStringLiteral("Usuário") && !e.hasSystemFallback;
        e.enabled = !trueValue(data.value(QStringLiteral("Hidden"))) &&
                    !falseValue(data.value(QStringLiteral("X-KDE-autostart-enabled")));
        QString applicabilityReason;
        const bool applicable = isApplicable(data, &applicabilityReason);
        // Entradas exclusivas de GNOME/outro DE ou TryExec ausente não pertencem à
        // lista de inicialização aplicável a esta sessão KDE.
        if (!applicable) continue;
        e.status = !e.enabled ? QStringLiteral("Desabilitado") :
                   !applicable ? QStringLiteral("Não aplicável") : QStringLiteral("Habilitado");
        e.notes = applicabilityReason;
        const QString condition = data.value(QStringLiteral("X-KDE-autostart-condition"));
        if (!condition.isEmpty()) {
            e.notes += (e.notes.isEmpty() ? QString{} : QStringLiteral(" · ")) +
                       QStringLiteral("Condição do KDE: ") + condition;
            if (e.enabled && applicable) e.status = QStringLiteral("Condicional");
        }
        all.append(std::move(e));
    }

    // O Plasma executa scripts de ambiente antes da sessão e scripts de logout ao sair.
    // Esses arquivos são consultados sem tentar alterar bits de execução ou executar scripts.
    const QString config = roots.first();
    struct ScriptDir { const char *relative; const char *kind; };
    for (const auto &spec : {ScriptDir{"plasma-workspace/env", "pre"},
                             ScriptDir{"autostart-scripts", "login"},
                             ScriptDir{"plasma-workspace/shutdown", "shutdown"}}) {
        const QString path = config + QLatin1Char('/') + QString::fromLatin1(spec.relative);
        const QDir dir(path);
        for (const QFileInfo &file : dir.entryInfoList(QDir::Files | QDir::System | QDir::NoDotAndDotDot,
                                                       QDir::Name | QDir::IgnoreCase)) {
            if (!file.isFile() && !file.isSymLink()) continue;
            Entry e;
            e.id = QStringLiteral("script:") + file.absoluteFilePath();
            e.name = file.fileName();
            e.icon = QStringLiteral("text-x-script");
            e.kind = QString::fromLatin1(spec.kind);
            e.path = file.absoluteFilePath();
            e.command = e.path;
            e.source = QStringLiteral("Usuário · Plasma");
            e.status = QStringLiteral("Configurado");
            e.enabled = true;
            e.notes = QStringLiteral("Script gerenciado pelo Plasma; edite-o nas Configurações do Sistema.");
            all.append(std::move(e));
        }
    }
    m_all = std::move(all);
    rebuild();
}

void StartupModel::rebuild()
{
    QVector<Entry> visible;
    auto matches = [&](const Entry &e) {
        return m_filter.isEmpty() || e.name.contains(m_filter, Qt::CaseInsensitive) ||
               e.command.contains(m_filter, Qt::CaseInsensitive) ||
               e.path.contains(m_filter, Qt::CaseInsensitive) ||
               e.status.contains(m_filter, Qt::CaseInsensitive);
    };
    struct Section { const char *kind; const char *label; };
    for (const auto &section : {Section{"application", "Aplicativos"},
                                Section{"pre", "Scripts anteriores à inicialização"},
                                Section{"login", "Scripts de inicialização"},
                                Section{"shutdown", "Scripts de desligamento"}}) {
        QVector<Entry> group;
        for (const Entry &e : std::as_const(m_all))
            if (e.kind == QString::fromLatin1(section.kind) && matches(e)) group.append(e);
        if (group.isEmpty()) continue;
        std::sort(group.begin(), group.end(), [](const Entry &a, const Entry &b) {
            return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
        });
        Entry header;
        header.header = true;
        header.name = QString::fromUtf8(section.label) + QStringLiteral(" (%1)").arg(group.size());
        visible.append(header);
        for (Entry &e : group) visible.append(std::move(e));
    }
    m_total = m_all.size();
    m_enabled = 0;
    m_scripts = 0;
    for (const Entry &e : std::as_const(m_all)) {
        if (e.enabled && e.kind == QStringLiteral("application")) ++m_enabled;
        if (e.kind != QStringLiteral("application")) ++m_scripts;
    }
    beginResetModel();
    m_visible = std::move(visible);
    endResetModel();
    emit countsChanged();
}

const StartupModel::Entry *StartupModel::findEntry(const QString &id) const
{
    for (const Entry &e : m_all) if (e.id == id) return &e;
    return nullptr;
}

bool StartupModel::setEnabled(const QString &id, bool enabled)
{
    const Entry *entry = findEntry(id);
    if (!entry || !id.startsWith(QStringLiteral("desktop:")) || !entry->editable) return false;
    if (entry->enabled == enabled) return true;
    const QString applicationName = entry->name;
    const QString destination = userAutostartDir() + QLatin1Char('/') + id.mid(QStringLiteral("desktop:").size());
    if (!QDir().mkpath(userAutostartDir())) {
        emit message(QStringLiteral("Não foi possível criar a pasta de inicialização do usuário."));
        return false;
    }
    const bool personal = entry->path == destination;
    const QString source = personal ? destination : entry->path;
    QByteArray bytes = readBoundedFile(source);
    if (bytes.isEmpty()) {
        emit message(QStringLiteral("Não foi possível ler o arquivo .desktop."));
        return false;
    }
    QByteArray changed;
    if (!rewriteDesktopKey(bytes, QStringLiteral("Hidden"), enabled ? QStringLiteral("false") : QStringLiteral("true"), &changed)) {
        emit message(QStringLiteral("Arquivo .desktop sem seção [Desktop Entry]."));
        return false;
    }
    // Respeita também entradas antigas do KDE que possuem este campo explícito.
    if (enabled && parseDesktop(changed).contains(QStringLiteral("X-KDE-autostart-enabled"))) {
        QByteArray corrected;
        if (!rewriteDesktopKey(changed, QStringLiteral("X-KDE-autostart-enabled"), QStringLiteral("true"), &corrected)) return false;
        changed = std::move(corrected);
    }
    if (!writeAtomically(destination, changed)) {
        emit message(QStringLiteral("Não foi possível salvar a entrada de inicialização."));
        return false;
    }
    refresh();
    emit message((enabled ? QStringLiteral("Inicialização habilitada: ") :
                           QStringLiteral("Inicialização desabilitada: ")) + applicationName +
                 QStringLiteral(" (efeito na próxima sessão)."));
    return true;
}

bool StartupModel::addApplication(const QString &name, const QString &command)
{
    const QString cleanName = name.trimmed(), cleanCommand = command.trimmed();
    if (cleanName.isEmpty() || cleanName.size() > 150 || cleanCommand.isEmpty() || cleanCommand.size() > 2048 ||
        cleanName.contains(QLatin1Char('\n')) || cleanCommand.contains(QLatin1Char('\n')) ||
        cleanName.contains(QLatin1Char('\r')) || cleanCommand.contains(QLatin1Char('\r'))) {
        emit message(QStringLiteral("Informe um nome e um comando válidos (sem quebras de linha)."));
        return false;
    }
    const QStringList parts = QProcess::splitCommand(cleanCommand);
    if (parts.isEmpty() || parts.first().isEmpty()) {
        emit message(QStringLiteral("O comando de inicialização é inválido."));
        return false;
    }
    if (!QDir().mkpath(userAutostartDir())) {
        emit message(QStringLiteral("Não foi possível criar a pasta de inicialização."));
        return false;
    }
    QString slug = cleanName.toLower();
    slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9_-]+")), QStringLiteral("-"));
    if (slug.isEmpty()) slug = QStringLiteral("aplicativo");
    QString fileName = QStringLiteral("tarefas11-") + slug + QStringLiteral(".desktop");
    for (int i = 2; QFileInfo::exists(userAutostartDir() + QLatin1Char('/') + fileName); ++i)
        fileName = QStringLiteral("tarefas11-") + slug + QStringLiteral("-%1.desktop").arg(i);
    const QString content = QStringLiteral("[Desktop Entry]\nType=Application\nName=") + cleanName +
                            QStringLiteral("\nExec=") + QString(cleanCommand).replace(QLatin1Char('%'), QStringLiteral("%%")) +
                            QStringLiteral("\nTerminal=false\nHidden=false\nX-Tarefas11-Created=true\n");
    if (!writeAtomically(userAutostartDir() + QLatin1Char('/') + fileName, content.toUtf8())) {
        emit message(QStringLiteral("Não foi possível criar o arquivo .desktop."));
        return false;
    }
    refresh();
    emit message(QStringLiteral("Adicionado ao início automático: ") + cleanName);
    return true;
}

bool StartupModel::importDesktopFile(const QUrl &url)
{
    if (!url.isLocalFile() || !url.toLocalFile().endsWith(QStringLiteral(".desktop"), Qt::CaseInsensitive)) {
        emit message(QStringLiteral("Selecione um arquivo local .desktop."));
        return false;
    }
    const QString source = url.toLocalFile();
    const QByteArray contents = readBoundedFile(source);
    const Fields fields = parseDesktop(contents);
    if (contents.isEmpty() || fields.value(QStringLiteral("Type")) != QStringLiteral("Application") ||
        fields.value(QStringLiteral("Exec")).isEmpty()) {
        emit message(QStringLiteral("O arquivo .desktop selecionado não é um aplicativo válido."));
        return false;
    }
    if (!QDir().mkpath(userAutostartDir())) return false;
    const QString destination = userAutostartDir() + QLatin1Char('/') + QFileInfo(source).fileName();
    if (QFileInfo::exists(destination)) {
        emit message(QStringLiteral("Já existe uma entrada com esse nome na inicialização do usuário."));
        return false;
    }
    QByteArray output;
    if (!rewriteDesktopKey(contents, QStringLiteral("Hidden"), QStringLiteral("false"), &output) ||
        !writeAtomically(destination, output)) {
        emit message(QStringLiteral("Não foi possível importar o arquivo .desktop."));
        return false;
    }
    refresh();
    emit message(QStringLiteral("Aplicativo importado para a inicialização do Plasma."));
    return true;
}

bool StartupModel::removeEntry(const QString &id)
{
    const Entry *entry = findEntry(id);
    if (!entry || !entry->removable) {
        emit message(QStringLiteral("Esta entrada não pode ser removida diretamente. Use habilitar/desabilitar."));
        return false;
    }
    const QString file = entry->path;
    if (!file.startsWith(userAutostartDir() + QLatin1Char('/')) || !QFile::remove(file)) {
        emit message(QStringLiteral("Não foi possível remover a entrada de inicialização."));
        return false;
    }
    refresh();
    emit message(QStringLiteral("Entrada removida do início automático; o aplicativo não foi desinstalado."));
    return true;
}

void StartupModel::openLocation(const QString &id)
{
    const Entry *entry = findEntry(id);
    if (!entry || !QFileInfo::exists(entry->path) ||
        !QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(entry->path).absolutePath()))) {
        emit message(QStringLiteral("Não foi possível abrir a pasta desta entrada."));
    }
}

void StartupModel::openKdeSettings()
{
    if (QProcess::startDetached(QStringLiteral("systemsettings"), {QStringLiteral("kcm_autostart")})) return;
    if (QProcess::startDetached(QStringLiteral("kcmshell6"), {QStringLiteral("kcm_autostart")})) return;
    emit message(QStringLiteral("Não foi possível abrir o módulo Iniciar automaticamente do KDE."));
}

QImage StartupIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QString spec = QUrl::fromPercentEncoding(id.toLatin1());
    QIcon icon = spec.startsWith(QLatin1Char('/')) ? QIcon(spec) : QIcon::fromTheme(spec);
    if (icon.isNull()) icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
    QImage image = icon.pixmap(requestedSize.isValid() ? requestedSize : QSize(48, 48)).toImage();
    if (image.isNull()) {
        image = QImage(48, 48, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(QStringLiteral("#7f90a9")));
    }
    if (size) *size = image.size();
    return image;
}
