#include "SataSmart.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QRegularExpression>
#include <cmath>
#include <limits>

namespace {
constexpr auto service = "org.freedesktop.UDisks2";
constexpr auto propertiesIface = "org.freedesktop.DBus.Properties";
constexpr auto blockIface = "org.freedesktop.UDisks2.Block";
constexpr auto ataIface = "org.freedesktop.UDisks2.Drive.Ata";

QVariant getProperty(QDBusInterface &properties, const QString &interfaceName, const QString &propertyName)
{
    const QDBusReply<QDBusVariant> reply = properties.call(
        QStringLiteral("Get"), interfaceName, propertyName);
    return reply.isValid() ? reply.value().variant() : QVariant{};
}
}

namespace SataSmart {
double celsiusFromKelvin(double kelvin, quint64 lastUpdateUnix)
{
    // UDisks2: 0 Kelvin ou SmartUpdated=0 significa leitura desconhecida.
    const double temp = kelvin - 273.15;
    if (lastUpdateUnix == 0 || !std::isfinite(kelvin) || kelvin <= 0 ||
        temp < -10.0 || temp > 125.0) return std::numeric_limits<double>::quiet_NaN();
    return temp;
}

QString blockPath(const QString &deviceName)
{
    static const QRegularExpression diskName(QStringLiteral("^sd[a-z]+$"));
    if (!diskName.match(deviceName).hasMatch()) return {};
    // Nomes básicos como sda/sdb não necessitam escaping adicional D-Bus.
    return QStringLiteral("/org/freedesktop/UDisks2/block_devices/") + deviceName;
}

QHash<QString, TemperatureSensors::Reading> sample(const QStringList &deviceNames)
{
    QHash<QString, TemperatureSensors::Reading> readings;
    const QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) return readings;

    for (const QString &device : deviceNames) {
        const QString path = blockPath(device);
        if (path.isEmpty()) continue;
        QDBusInterface blockProperties(QString::fromLatin1(service), path,
            QString::fromLatin1(propertiesIface), bus);
        blockProperties.setTimeout(900);
        const QVariant driveValue = getProperty(blockProperties,
            QString::fromLatin1(blockIface), QStringLiteral("Drive"));
        if (!driveValue.isValid()) continue;
        QString drivePath = driveValue.value<QDBusObjectPath>().path();
        if (drivePath.isEmpty()) drivePath = driveValue.toString();
        if (!drivePath.startsWith(QStringLiteral("/org/freedesktop/UDisks2/drives/"))) continue;

        QDBusInterface driveProperties(QString::fromLatin1(service), drivePath,
            QString::fromLatin1(propertiesIface), bus);
        driveProperties.setTimeout(900);
        const QVariant updatedValue = getProperty(driveProperties,
            QString::fromLatin1(ataIface), QStringLiteral("SmartUpdated"));
        const QVariant kelvinValue = getProperty(driveProperties,
            QString::fromLatin1(ataIface), QStringLiteral("SmartTemperature"));
        if (!updatedValue.isValid() || !kelvinValue.isValid()) continue;

        bool updatedOk = false, kelvinOk = false;
        const quint64 updated = updatedValue.toULongLong(&updatedOk);
        const double kelvin = kelvinValue.toDouble(&kelvinOk);
        if (!updatedOk || !kelvinOk) continue;
        const double celsius = celsiusFromKelvin(kelvin, updated);
        if (std::isfinite(celsius)) {
            readings.insert(device, {celsius, QStringLiteral("S.M.A.R.T. / UDisks2 (ATA)")});
        }
    }
    return readings;
}
}
