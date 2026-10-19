#pragma once

#include <QHash>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <functional>
#include <QImage>
#include <QQuickImageProvider>
#include <QReadWriteLock>
#include <QSet>
#include <QStringList>

struct AppIdentity
{
    QString displayName;
    QString platform;        // Linux ou Windows (Wine/Proton)
    QString iconUrl;
    QString iconSource;      // Origem real do ícone
    QString executablePath; // PE real, quando identificável
    QString desktopFile;
    bool desktopMatched = false;
    bool windows = false;
};

class AppIdentityResolver
{
public:
    AppIdentityResolver();
    // Metadados leves: sem abrir executáveis PE nem gerar QPixmap.
    AppIdentity quickIdentity(const QString &comm, const QStringList &args);
    AppIdentity resolve(int pid, const QString &comm, const QStringList &args,
                        const QString &linuxExecutable);
    QImage imageForId(const QString &id, const QSize &size) const;
    // A indexação XDG é assíncrona: identidade/ícones são reavaliados após concluir.
    void setIndexReadyCallback(std::function<void()> callback);
    bool isDesktopIndexReady() const { return m_desktopIndexReady; }

private:
    struct Desktop { QString name, icon, file; };
    void loadDesktopEntries();
    QString saveIcon(const QString &cacheKey, const QImage &image);
    QString icon(const QString &spec, const QString &fallbackKey);
    QString findWindowsExecutable(int pid, const QStringList &args,
                                  const QString &linuxExecutable) const;
    static QImage genericIcon(bool windows);
    using DesktopIndex = QHash<QString, Desktop>;
    DesktopIndex m_desktopByKey;
    QFutureWatcher<DesktopIndex> m_desktopWatcher;
    QElapsedTimer m_indexElapsed;
    std::function<void()> m_indexReadyCallback;
    bool m_desktopIndexReady = false;
    QHash<QString, QString> m_iconUrls;
    QHash<QString, QImage> m_images;
    QSet<QString> m_missingPeIcons;
    mutable QReadWriteLock m_lock;
};

// Renderização do QIcon no thread de interface; o provider apenas devolve imagens cacheadas.
class AppIconProvider final : public QQuickImageProvider
{
public:
    explicit AppIconProvider(AppIdentityResolver *resolver)
        : QQuickImageProvider(QQuickImageProvider::Image), m_resolver(resolver) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
private:
    AppIdentityResolver *m_resolver;
};
