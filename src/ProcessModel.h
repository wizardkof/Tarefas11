#pragma once

#include "AppIdentity.h"
#include "TcpSampler.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <QVector>

class ProcessModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
    Q_PROPERTY(QString sortField READ sortField WRITE setSortField NOTIFY sortChanged)
    Q_PROPERTY(QString typeFilter READ typeFilter WRITE setTypeFilter NOTIFY typeFilterChanged)
    Q_PROPERTY(bool groupMode READ groupMode WRITE setGroupMode NOTIFY groupModeChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged)
    Q_PROPERTY(int windowsCount READ windowsCount NOTIFY totalCountChanged)
    Q_PROPERTY(int appsCount READ appsCount NOTIFY totalCountChanged)
    Q_PROPERTY(int threadCount READ threadCount NOTIFY totalCountChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY totalCountChanged)
    Q_PROPERTY(QVariantMap selectedInfo READ selectedInfo NOTIFY selectedInfoChanged)

public:
    enum Roles {
        PidRole = Qt::UserRole + 1,
        ProcessNameRole, CpuRole, MemoryRole, DiskRole, IoAvailableRole,
        UserRole, StateRole, CommandRole, StartTimeRole, ThreadsRole,
        DisplayNameRole, IconUrlRole, IconSourceRole, PlatformRole, ExePathRole,
        DesktopFileRole, PpidRole, NiceRole, VirtualMiBRole, SharedMiBRole,
        ReadMiBsRole, WriteMiBsRole, CpuTimeSecondsRole, IsAppRole, IsWindowsRole,
        RowTypeRole, IsHeaderRole, CategoryKeyRole, CategoryTitleRole,
        LevelRole, ExpandableRole, ExpandedRole, ChildCountRole,
        IoShareRole, NetworkRateRole, NetworkAvailableRole, NetworkRxRole, NetworkTxRole,
        AggregatedRole, GroupProcessCountRole, OwnMemoryRole,
        SwapMiBRole, SwapAvailableRole
    };
    Q_ENUM(Roles)

    explicit ProcessModel(AppIdentityResolver *resolver, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filterText() const { return m_filter; }
    void setFilterText(const QString &value);
    QString sortField() const { return m_sortField; }
    void setSortField(const QString &value);
    QString typeFilter() const { return m_typeFilter; }
    void setTypeFilter(const QString &value);
    bool groupMode() const { return m_groupMode; }
    void setGroupMode(bool value);
    int totalCount() const { return m_all.size(); }
    int visibleCount() const { return m_visibleProcessCount; }
    int windowsCount() const { return m_windowsCount; }
    int appsCount() const { return m_appsCount; }
    int threadCount() const { return m_threadCount; }
    QVariantMap selectedInfo() const;

    Q_INVOKABLE bool terminate(int pid, qulonglong startTime, bool force = false);
    Q_INVOKABLE bool reducePriority(int pid, qulonglong startTime);
    Q_INVOKABLE bool launchCommand(const QString &command);
    Q_INVOKABLE QString formatMemoryMiB(double valueMiB) const;
    Q_INVOKABLE void copyPid(int pid);
    Q_INVOKABLE void copyCommand(int pid, qulonglong startTime);
    Q_INVOKABLE void openContainingFolder(int pid, qulonglong startTime);
    Q_INVOKABLE void selectProcess(int pid, qulonglong startTime);
    Q_INVOKABLE void toggleExpanded(int pid, qulonglong startTime);
    Q_INVOKABLE bool isExpanded(int pid, qulonglong startTime) const;
    Q_INVOKABLE void toggleCategory(const QString &categoryKey);
    Q_INVOKABLE bool isCategoryCollapsed(const QString &categoryKey) const;
    Q_INVOKABLE QString rowKeyAt(int row) const;
    Q_INVOKABLE int indexOfRow(const QString &key) const;

public slots:
    void refresh(quint64 hostCpuTicks);

signals:
    void filterChanged();
    void sortChanged();
    void typeFilterChanged();
    void groupModeChanged();
    void totalCountChanged();
    void selectedInfoChanged();
    void actionError(const QString &message);
    void actionSuccess(const QString &message);

private:
    struct Entry {
        int pid = 0, ppid = 0, nice = 0, threads = 0;
        QString name, command, user, state;
        QString identitySignature;
        QStringList arguments;
        AppIdentity identity;
        double cpu = 0, memoryMiB = 0, swapMiB = 0, virtualMiB = 0, sharedMiB = 0;
        bool swapAvailable = false;
        double diskMiBs = 0, readMiBs = 0, writeMiBs = 0, cpuTimeSeconds = 0;
        double ioSharePercent = 0, networkMiBs = 0, networkRxMiBs = 0, networkTxMiBs = 0;
        bool networkAvailable = false;
        quint64 startTime = 0, cpuTicks = 0, readBytes = 0, writeBytes = 0;
        bool ioAvailable = false;
    };
    struct CachedIdentity {
        quint64 startTime = 0;
        QString signature;
        AppIdentity value;
        bool complete = false;
        bool desktopIndexed = false;
    };
    struct Row {
        bool header = false;
        QString categoryKey;
        QString categoryTitle;
        int categoryCount = 0;
        int level = 0;
        bool expandable = false;
        bool expanded = false;
        int childCount = 0;
        bool aggregated = false;
        int groupProcessCount = 1;
        double ownMemoryMiB = 0;
        Entry entry;
    };

    bool validateProcess(int pid, quint64 startTime);
    const Entry *selectedEntry() const;
    void rebuild();
    void scheduleIdentityUpdates();
    void resolveIdentityBatch();
    void desktopIndexAvailable();
    QString processKey(int pid, quint64 startTime) const;
    QString classifyCategory(const Entry &entry) const;
    QString categoryTitle(const QString &key) const;

    AppIdentityResolver *m_resolver;
    TcpSampler m_tcpSampler;
    QVector<Entry> m_all;
    QVector<Row> m_rows;
    QHash<int, CachedIdentity> m_identityCache;
    QTimer m_identityTimer;
    QVector<int> m_identityQueue; // Índices de m_all, sempre reconstruídos após refresh.
    int m_identityPosition = 0;
    QElapsedTimer m_identityElapsed;
    QHash<int, quint64> m_previousCpu, m_previousStartTime, m_previousRead, m_previousWrite;
    QHash<uint, QString> m_userNames;
    QElapsedTimer m_elapsed;
    quint64 m_previousHostCpu = 0;
    QString m_filter, m_typeFilter = QStringLiteral("all"), m_sortField = QStringLiteral("name");
    bool m_groupMode = true;
    int m_windowsCount = 0, m_appsCount = 0, m_threadCount = 0, m_visibleProcessCount = 0;
    int m_selectedPid = -1;
    quint64 m_selectedStart = 0;
    QSet<QString> m_expanded;
    QSet<QString> m_collapsedCategories;
};
