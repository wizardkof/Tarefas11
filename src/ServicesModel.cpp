#include "ServicesModel.h"

#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <memory>
#include <utility>

ServicesModel::ServicesModel(QObject *parent) : QAbstractListModel(parent) {}
int ServicesModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_visible.size(); }
QVariant ServicesModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() >= m_visible.size() || idx.row() < 0) return {};
    const Entry &e = m_visible.at(idx.row());
    switch (role) {
    case NameRole: return e.name;
    case DescriptionRole: return e.description;
    case ScopeRole: return e.scope;
    case LoadRole: return e.load;
    case ActiveRole: return e.active;
    case SubRole: return e.sub;
    case StatusRole: return e.active == QStringLiteral("active") ? QStringLiteral("Em execução") :
                             e.active == QStringLiteral("failed") ? QStringLiteral("Falhou") :
                             e.active == QStringLiteral("activating") ? QStringLiteral("Iniciando") :
                             e.active == QStringLiteral("deactivating") ? QStringLiteral("Parando") : QStringLiteral("Parado");
    case MainPidRole: return e.mainPid ? QString::number(e.mainPid) : QStringLiteral("—");
    default: return {};
    }
}
QHash<int, QByteArray> ServicesModel::roleNames() const
{
    return {{NameRole,"unitName"},{DescriptionRole,"description"},{ScopeRole,"scope"},
            {LoadRole,"loadState"},{ActiveRole,"activeState"},{SubRole,"subState"},
            {StatusRole,"statusText"},{MainPidRole,"mainPidText"}};
}
void ServicesModel::setFilterText(const QString &v) { if (v != m_filterText) { m_filterText = v; buildVisible(); emit filterChanged(); } }
void ServicesModel::setScopeFilter(const QString &v) { if (v != m_scopeFilter) { m_scopeFilter = v; buildVisible(); emit filterChanged(); } }
void ServicesModel::setStateFilter(const QString &v) { if (v != m_stateFilter) { m_stateFilter = v; buildVisible(); emit filterChanged(); } }
int ServicesModel::runningCount() const
{
    return std::count_if(m_entries.cbegin(), m_entries.cend(), [](const Entry &e) { return e.active == QStringLiteral("active"); });
}
void ServicesModel::buildVisible()
{
    QVector<Entry> next;
    for (const Entry &e : std::as_const(m_entries)) {
        if (m_scopeFilter != QStringLiteral("all") && e.scope != m_scopeFilter) continue;
        if (m_stateFilter == QStringLiteral("active") && e.active != QStringLiteral("active")) continue;
        if (m_stateFilter == QStringLiteral("inactive") && e.active == QStringLiteral("active")) continue;
        if (!m_filterText.isEmpty() && !e.name.contains(m_filterText, Qt::CaseInsensitive) &&
            !e.description.contains(m_filterText, Qt::CaseInsensitive)) continue;
        next.append(e);
    }
    std::sort(next.begin(), next.end(), [](const Entry &a, const Entry &b) {
        int c = QString::compare(a.name, b.name, Qt::CaseInsensitive);
        return c == 0 ? a.scope < b.scope : c < 0;
    });
    beginResetModel();
    m_visible = std::move(next);
    endResetModel();
}
QVector<ServicesModel::Entry> ServicesModel::parseUnits(const QByteArray &data, const QString &scope)
{
    QVector<Entry> entries;
    // Alguns systemctl recentes suportam JSON; quando não suportam, usar saída tabular.
    QJsonParseError error;
    const QJsonDocument json = QJsonDocument::fromJson(data, &error);
    if (error.error == QJsonParseError::NoError && json.isArray()) {
        for (const auto &v : json.array()) {
            const QJsonObject o = v.toObject();
            Entry e;
            e.name = o.value(QStringLiteral("unit")).toString(o.value(QStringLiteral("name")).toString());
            e.description = o.value(QStringLiteral("description")).toString();
            e.load = o.value(QStringLiteral("load")).toString();
            e.active = o.value(QStringLiteral("active")).toString();
            e.sub = o.value(QStringLiteral("sub")).toString();
            e.scope = scope;
            if (e.name.endsWith(QStringLiteral(".service"))) entries.append(std::move(e));
        }
        return entries;
    }
    // --full --plain --no-legend: UNIT LOAD ACTIVE SUB DESCRIPTION.
    static const QRegularExpression rx(QStringLiteral(R"(^\s*(?:[●×]\s*)?(\S+\.service)\s+(\S+)\s+(\S+)\s+(\S+)\s*(.*)$)"));
    for (const QByteArray &raw : data.split('\n')) {
        const QString line = QString::fromUtf8(raw);
        const auto match = rx.match(line);
        if (!match.hasMatch()) continue;
        Entry e;
        e.name = match.captured(1);
        e.load = match.captured(2);
        e.active = match.captured(3);
        e.sub = match.captured(4);
        e.description = match.captured(5).trimmed();
        e.scope = scope;
        entries.append(std::move(e));
    }
    return entries;
}
void ServicesModel::refresh()
{
    if (m_busy) return;
    m_busy = true;
    m_pending = 2;
    m_lastError.clear();
    m_available = false;
    m_entries.clear();
    emit stateChanged();
    queryScope(QStringLiteral("system"));
    queryScope(QStringLiteral("user"));
}
void ServicesModel::queryScope(const QString &scope)
{
    auto *p = new QProcess(this);
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("SYSTEMD_COLORS"), QStringLiteral("0"));
    env.insert(QStringLiteral("SYSTEMD_PAGER"), QStringLiteral("cat"));
    p->setProcessEnvironment(env);
    p->setProgram(QStringLiteral("systemctl"));
    p->setArguments({scope == QStringLiteral("system") ? QStringLiteral("--system") : QStringLiteral("--user"),
                     QStringLiteral("list-units"), QStringLiteral("--type=service"), QStringLiteral("--all"),
                     QStringLiteral("--full"), QStringLiteral("--plain"),
                     QStringLiteral("--no-legend"), QStringLiteral("--no-pager")});
    p->setProcessChannelMode(QProcess::SeparateChannels);
    const auto finished = std::make_shared<bool>(false);
    auto complete = [this, p, scope, finished](bool success) {
        if (*finished) return;
        *finished = true;
        if (success) {
            auto parsed = parseUnits(p->readAllStandardOutput(), scope);
            m_available = true;
            for (auto &entry : parsed) m_entries.append(std::move(entry));
        } else {
            const QString msg = QString::fromLocal8Bit(p->readAllStandardError()).trimmed();
            if (!m_lastError.isEmpty()) m_lastError += QStringLiteral(" | ");
            m_lastError += scope + QStringLiteral(": ") + (msg.isEmpty() ? QStringLiteral("systemctl indisponível") : msg.left(240));
        }
        p->deleteLater();
        --m_pending;
        if (m_pending == 0) {
            m_busy = false;
            buildVisible();
            emit countsChanged();
            emit stateChanged();
        }
    };
    QObject::connect(p, &QProcess::finished, this, [complete](int code, QProcess::ExitStatus status) {
        complete(code == 0 && status == QProcess::NormalExit);
    });
    QObject::connect(p, &QProcess::errorOccurred, this, [complete](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete(false);
    });
    p->start();
    QTimer::singleShot(5000, p, [p] { if (p->state() != QProcess::NotRunning) p->kill(); });
}
void ServicesModel::inspectService(const QString &name, const QString &scope)
{
    const auto valid = std::find_if(m_entries.cbegin(), m_entries.cend(), [&](const Entry &e) {
        return e.name == name && e.scope == scope;
    });
    if (valid == m_entries.cend()) return;
    auto *p = new QProcess(this);
    p->setProgram(QStringLiteral("systemctl"));
    p->setArguments({scope == QStringLiteral("system") ? QStringLiteral("--system") : QStringLiteral("--user"),
                     QStringLiteral("show"), QStringLiteral("--property=MainPID"),
                     QStringLiteral("--value"), QStringLiteral("--"), name});
    QObject::connect(p, &QProcess::finished, this, [this, p, name, scope](int exitCode, QProcess::ExitStatus status) {
        if (exitCode == 0 && status == QProcess::NormalExit) {
            bool ok = false;
            const int pid = QString::fromUtf8(p->readAllStandardOutput()).trimmed().toInt(&ok);
            if (ok && pid > 0) {
                for (auto &entry : m_entries) {
                    if (entry.name == name && entry.scope == scope) entry.mainPid = pid;
                }
                for (int row = 0; row < m_visible.size(); ++row) {
                    Entry &e = m_visible[row];
                    if (e.name == name && e.scope == scope) {
                        e.mainPid = pid;
                        emit dataChanged(index(row, 0), index(row, 0), {MainPidRole});
                    }
                }
            }
        }
        p->deleteLater();
    });
    QObject::connect(p, &QProcess::errorOccurred, p, [p](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) p->deleteLater();
    });
    p->start();
    QTimer::singleShot(3000, p, [p] { if (p->state() != QProcess::NotRunning) p->kill(); });
}

void ServicesModel::changeService(const QString &name, const QString &scope, const QString &operation)
{
    if (!name.endsWith(QStringLiteral(".service")) || name.size() > 200 ||
        name.contains(QRegularExpression(QStringLiteral("\\s"))) ||
        (scope != QStringLiteral("user") && scope != QStringLiteral("system")) ||
        (operation != QStringLiteral("start") && operation != QStringLiteral("stop") && operation != QStringLiteral("restart"))) {
        emit message(QStringLiteral("Operação rejeitada: serviço ou ação inválida."));
        return;
    }
    // Só aceitar unidades vistas no inventário, nunca um nome arbitrário do QML.
    const auto unit = std::find_if(m_entries.cbegin(), m_entries.cend(), [&](const Entry &e) { return e.name == name && e.scope == scope; });
    if (unit == m_entries.cend()) { emit message(QStringLiteral("Serviço não está mais na lista. Atualize a página.")); return; }
    auto *p = new QProcess(this);
    p->setProgram(QStringLiteral("systemctl"));
    p->setArguments({scope == QStringLiteral("system") ? QStringLiteral("--system") : QStringLiteral("--user"),
                     QStringLiteral("--no-ask-password"), QStringLiteral("--no-pager"), operation, QStringLiteral("--"), name});
    auto handled = std::make_shared<bool>(false);
    auto complete = [this, p, operation, name, handled](bool success) {
        if (*handled) return;
        *handled = true;
        const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
        emit message(success
                         ? QStringLiteral("Operação %1 solicitada a %2.").arg(operation, name)
                         : QStringLiteral("Não foi possível executar %1 em %2: %3").arg(operation, name,
                               err.isEmpty() ? QStringLiteral("systemctl indisponível ou sem autorização") : err.left(250)));
        p->deleteLater();
        refresh();
    };
    QObject::connect(p, &QProcess::finished, this, [complete](int code, QProcess::ExitStatus status) {
        complete(code == 0 && status == QProcess::NormalExit);
    });
    QObject::connect(p, &QProcess::errorOccurred, this, [complete](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete(false);
    });
    p->start();
    QTimer::singleShot(12000, p, [p] { if (p->state() != QProcess::NotRunning) p->kill(); });
}
