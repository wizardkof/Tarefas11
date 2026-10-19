#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QVector>

// Contas locais/NSS; indicadores de processo são extraídos de /proc.
class UsersModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
    Q_PROPERTY(bool showSystemUsers READ showSystemUsers WRITE setShowSystemUsers NOTIFY filterChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countsChanged)
    Q_PROPERTY(bool sessionsKnown READ sessionsKnown NOTIFY countsChanged)
public:
    enum Roles { UserNameRole = Qt::UserRole + 1, UidRole, FullNameRole,
                 HomeRole, ShellRole, GroupsRole, ProcessesRole, MemoryRole, SessionRole };
    explicit UsersModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString filterText() const { return m_filter; }
    void setFilterText(const QString &filter);
    bool showSystemUsers() const { return m_showSystem; }
    void setShowSystemUsers(bool value);
    int totalCount() const { return m_visible.size(); }
    bool sessionsKnown() const { return m_sessionsKnown; }
    Q_INVOKABLE void refresh();
signals:
    void filterChanged();
    void countsChanged();
private:
    struct Entry {
        QString user, fullName, home, shell, groups;
        uint uid = 0;
        int processCount = 0;
        double memoryMiB = 0;
    };
    QString m_filter;
    bool m_showSystem = false, m_sessionsKnown = false;
    QVector<Entry> m_all, m_visible;
    QSet<uint> m_loggedInUids;
    void rebuild();
    void fetchSessions();
};
