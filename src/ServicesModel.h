#pragma once

#include <QAbstractListModel>
#include <QProcess>
#include <QString>
#include <QVector>

// Retratos reais das unidades systemd. Nenhum serviço fictício é sintetizado.
class ServicesModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
    Q_PROPERTY(QString scopeFilter READ scopeFilter WRITE setScopeFilter NOTIFY filterChanged)
    Q_PROPERTY(QString stateFilter READ stateFilter WRITE setStateFilter NOTIFY filterChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countsChanged)
    Q_PROPERTY(int runningCount READ runningCount NOTIFY countsChanged)
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
public:
    enum Roles { NameRole = Qt::UserRole + 1, DescriptionRole, ScopeRole,
                 LoadRole, ActiveRole, SubRole, StatusRole, MainPidRole };
    explicit ServicesModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString filterText() const { return m_filterText; }
    QString scopeFilter() const { return m_scopeFilter; }
    QString stateFilter() const { return m_stateFilter; }
    void setFilterText(const QString &text);
    void setScopeFilter(const QString &filter);
    void setStateFilter(const QString &filter);
    bool busy() const { return m_busy; }
    QString lastError() const { return m_lastError; }
    bool available() const { return m_available; }
    int totalCount() const { return m_entries.size(); }
    int runningCount() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void inspectService(const QString &name, const QString &scope);
    Q_INVOKABLE void changeService(const QString &name, const QString &scope, const QString &operation);
signals:
    void filterChanged();
    void countsChanged();
    void stateChanged();
    void message(const QString &text);
private:
    struct Entry {
        QString name, description, scope, load, active, sub;
        int mainPid = 0;
    };
    QVector<Entry> m_entries, m_visible;
    QString m_filterText, m_scopeFilter = QStringLiteral("all"), m_stateFilter = QStringLiteral("all");
    QString m_lastError;
    bool m_busy = false, m_available = false;
    int m_pending = 0;
    void buildVisible();
    void queryScope(const QString &scope);
    static QVector<Entry> parseUnits(const QByteArray &data, const QString &scope);
};
