#include "UsersModel.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>
#include <memory>
#include <utility>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#include <vector>

UsersModel::UsersModel(QObject *parent) : QAbstractListModel(parent) {}
int UsersModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_visible.size(); }
QVariant UsersModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() < 0 || idx.row() >= m_visible.size()) return {};
    const Entry &e = m_visible.at(idx.row());
    switch (role) {
    case UserNameRole: return e.user;
    case UidRole: return static_cast<qulonglong>(e.uid);
    case FullNameRole: return e.fullName;
    case HomeRole: return e.home;
    case ShellRole: return e.shell;
    case GroupsRole: return e.groups;
    case ProcessesRole: return e.processCount;
    case MemoryRole: return e.memoryMiB;
    case SessionRole: return !m_sessionsKnown ? QStringLiteral("Não disponível") :
                             m_loggedInUids.contains(e.uid) ? QStringLiteral("Com sessão") : QStringLiteral("Sem sessão");
    default: return {};
    }
}
QHash<int, QByteArray> UsersModel::roleNames() const
{
    return {{UserNameRole,"userName"},{UidRole,"uidValue"},
            {FullNameRole,"fullName"},{HomeRole,"homeDir"},{ShellRole,"shellPath"},
            {GroupsRole,"groupsText"},{ProcessesRole,"processCount"},
            {MemoryRole,"memoryMiB"},{SessionRole,"sessionText"}};
}
void UsersModel::setFilterText(const QString &v) { if (m_filter != v) { m_filter = v; rebuild(); emit filterChanged(); } }
void UsersModel::setShowSystemUsers(bool v) { if (m_showSystem != v) { m_showSystem = v; rebuild(); emit filterChanged(); } }
void UsersModel::rebuild()
{
    QVector<Entry> next;
    for (const Entry &e : std::as_const(m_all)) {
        if (!m_showSystem && e.uid < 1000 && e.uid != static_cast<uint>(geteuid())) continue;
        if (!m_showSystem && e.uid == 65534) continue;
        if (!m_filter.isEmpty() && !e.user.contains(m_filter, Qt::CaseInsensitive) &&
            !e.fullName.contains(m_filter, Qt::CaseInsensitive) && !QString::number(e.uid).contains(m_filter)) continue;
        next.append(e);
    }
    std::sort(next.begin(), next.end(), [](const Entry &a, const Entry &b) { return a.user.toCaseFolded() < b.user.toCaseFolded(); });
    beginResetModel();
    m_visible = std::move(next);
    endResetModel();
    emit countsChanged();
}
void UsersModel::refresh()
{
    // /etc/passwd e NSS: o usuário pode visualizar outros UIDs, mas não modificar contas por aqui.
    QVector<Entry> next;
    setpwent();
    for (int i = 0; i < 20000; ++i) {
        const passwd *pw = getpwent();
        if (!pw) break;
        Entry e;
        e.user = QString::fromLocal8Bit(pw->pw_name ? pw->pw_name : "");
        e.uid = pw->pw_uid;
        e.fullName = QString::fromLocal8Bit(pw->pw_gecos ? pw->pw_gecos : "").section(',', 0, 0);
        e.home = QString::fromLocal8Bit(pw->pw_dir ? pw->pw_dir : "");
        e.shell = QString::fromLocal8Bit(pw->pw_shell ? pw->pw_shell : "");
        std::vector<gid_t> groupIds(16);
        int required = static_cast<int>(groupIds.size());
        QByteArray local = e.user.toLocal8Bit();
        if (getgrouplist(local.constData(), pw->pw_gid, groupIds.data(), &required) == -1 && required > 0 && required < 512) {
            groupIds.resize(required);
            getgrouplist(local.constData(), pw->pw_gid, groupIds.data(), &required);
        }
        QStringList groupNames;
        for (int n = 0; n < std::min(required, static_cast<int>(groupIds.size())); ++n) {
            const group *g = getgrgid(groupIds[n]);
            const QString name = g && g->gr_name ? QString::fromLocal8Bit(g->gr_name) : QString::number(groupIds[n]);
            if (!groupNames.contains(name)) groupNames.append(name);
        }
        groupNames.sort(Qt::CaseInsensitive);
        e.groups = groupNames.join(QStringLiteral(", "));
        if (!e.user.isEmpty()) next.append(std::move(e));
    }
    endpwent();

    QHash<uint, QPair<int, double>> resourceMap;
    const QStringList dirs = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : dirs) {
        bool ok = false;
        pid.toUInt(&ok);
        if (!ok) continue;
        QFile f(QStringLiteral("/proc/%1/status").arg(pid));
        if (!f.open(QIODevice::ReadOnly)) continue;
        uint uid = static_cast<uint>(-1);
        double memMiB = 0;
        while (!f.atEnd()) {
            const QByteArray line = f.readLine();
            if (line.startsWith("Uid:")) {
                const auto parts = line.mid(4).simplified().split(' ');
                if (!parts.isEmpty()) uid = parts.at(0).toUInt();
            } else if (line.startsWith("VmRSS:")) {
                const auto parts = line.mid(6).simplified().split(' ');
                if (!parts.isEmpty()) memMiB = parts.at(0).toDouble() / 1024.0;
            }
        }
        if (uid != static_cast<uint>(-1)) {
            auto &value = resourceMap[uid];
            ++value.first;
            value.second += memMiB;
        }
    }
    for (Entry &e : next) {
        const auto val = resourceMap.value(e.uid);
        e.processCount = val.first;
        e.memoryMiB = val.second;
    }
    m_all = std::move(next);
    rebuild();
    fetchSessions();
}
void UsersModel::fetchSessions()
{
    auto *p = new QProcess(this);
    p->setProgram(QStringLiteral("loginctl"));
    p->setArguments({QStringLiteral("list-users"), QStringLiteral("--no-legend"), QStringLiteral("--no-pager")});
    auto handled = std::make_shared<bool>(false);
    auto complete = [this, p, handled](bool success) {
        if (*handled) return;
        *handled = true;
        QSet<uint> ids;
        if (success) {
            for (const QByteArray &line : p->readAllStandardOutput().split('\n')) {
                const auto pieces = line.simplified().split(' ');
                if (pieces.isEmpty()) continue;
                bool ok = false;
                const uint uid = pieces.at(0).toUInt(&ok);
                if (ok) ids.insert(uid);
            }
            m_loggedInUids = std::move(ids);
            m_sessionsKnown = true;
        } else {
            m_sessionsKnown = false;
            m_loggedInUids.clear();
        }
        p->deleteLater();
        if (!m_visible.isEmpty()) emit dataChanged(index(0, 0), index(m_visible.size() - 1, 0), {SessionRole});
        emit countsChanged();
    };
    QObject::connect(p, &QProcess::finished, this, [complete](int code, QProcess::ExitStatus status) {
        complete(code == 0 && status == QProcess::NormalExit);
    });
    QObject::connect(p, &QProcess::errorOccurred, this, [complete](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete(false);
    });
    p->start();
    QTimer::singleShot(3500, p, [p] { if (p->state() != QProcess::NotRunning) p->kill(); });
}
