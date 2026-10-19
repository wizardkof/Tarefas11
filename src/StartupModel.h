#pragma once

#include <QAbstractListModel>
#include <QQuickImageProvider>
#include <QString>
#include <QVector>
#include <QUrl>

class StartupModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countsChanged)
    Q_PROPERTY(int enabledCount READ enabledCount NOTIFY countsChanged)
    Q_PROPERTY(int scriptsCount READ scriptsCount NOTIFY countsChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1, NameRole, IconUrlRole, CommandRole,
        StatusRole, SourceRole, PathRole, KindRole, EnabledRole,
        EditableRole, RemovableRole, HeaderRole, NotesRole
    };
    Q_ENUM(Roles)
    explicit StartupModel(QObject *parent = nullptr, bool eagerLoad = true);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filterText() const { return m_filter; }
    void setFilterText(const QString &filter);
    int totalCount() const { return m_total; }
    int enabledCount() const { return m_enabled; }
    int scriptsCount() const { return m_scripts; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE bool addApplication(const QString &name, const QString &command);
    Q_INVOKABLE bool importDesktopFile(const QUrl &url);
    Q_INVOKABLE bool removeEntry(const QString &id);
    Q_INVOKABLE void openLocation(const QString &id);
    Q_INVOKABLE void openKdeSettings();

signals:
    void filterChanged();
    void countsChanged();
    void message(const QString &message);

private:
    struct Entry {
        QString id, name, icon, command, status, source, path, kind, notes;
        bool enabled = false, editable = false, removable = false, header = false;
        bool hasSystemFallback = false;
    };
    const Entry *findEntry(const QString &id) const;
    QString userAutostartDir() const;
    void rebuild();

    QString m_filter;
    QVector<Entry> m_all;
    QVector<Entry> m_visible;
    int m_total = 0, m_enabled = 0, m_scripts = 0;
};

class StartupIconProvider final : public QQuickImageProvider
{
public:
    StartupIconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
