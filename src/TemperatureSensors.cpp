#include "TemperatureSensors.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace {
QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.read(1024)).trimmed() : QString{};
}

TemperatureSensors::Reading input(const QString &path, const QString &description)
{
    bool ok = false;
    const double milli = readText(path).toDouble(&ok);
    if (!ok || milli < -10000.0 || milli > 125000.0) return {};
    return {milli / 1000.0, description};
}

QStringList monitorDirs(const QString &root)
{
    return QDir(root).entryList({QStringLiteral("hwmon*")},
        QDir::Dirs | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
}

bool atOrUnder(const QString &path, const QString &ancestor)
{
    return !path.isEmpty() && !ancestor.isEmpty() &&
        (path == ancestor || path.startsWith(ancestor + QLatin1Char('/')));
}
}

namespace TemperatureSensors {
Reading cpu(const QString &hwmonRoot)
{
    Reading best;
    int bestScore = -1;
    for (const QString &node : monitorDirs(hwmonRoot)) {
        const QString dir = QDir(hwmonRoot).filePath(node);
        const QString driver = readText(dir + QStringLiteral("/name")).toLower();
        if (driver != QStringLiteral("k10temp") && driver != QStringLiteral("zenpower") &&
            driver != QStringLiteral("coretemp") && driver != QStringLiteral("peci_cputemp") &&
            driver != QStringLiteral("cpu_thermal") && driver != QStringLiteral("x86_pkg_temp")) continue;
        const QStringList inputs = QDir(dir).entryList({QStringLiteral("temp*_input")}, QDir::Files, QDir::Name);
        for (const QString &sensor : inputs) {
            const QString label = readText(dir + QLatin1Char('/') + sensor.left(sensor.size() - 5) + QStringLiteral("label"));
            const QString normalized = label.toLower();
            int score = 1;
            if (normalized.contains(QStringLiteral("tdie"))) score = 100;
            else if (normalized.contains(QStringLiteral("package")) || normalized.contains(QStringLiteral("tccd1"))) score = 80;
            else if (normalized.contains(QStringLiteral("tctl"))) score = 60;
            else if (normalized.contains(QStringLiteral("core"))) score = 40;
            const auto result = input(dir + QLatin1Char('/') + sensor,
                driver + (label.isEmpty() ? QString{} : QStringLiteral(" / ") + label));
            if (result.valid() && score > bestScore) { bestScore = score; best = result; }
        }
    }
    return best;
}

Reading disk(const QString &deviceName, const QString &hwmonRoot, const QString &nvmeRoot, const QString &blockRoot)
{
    // Alguns SSDs SATA publicam drivetemp no hwmon. Associar pelo caminho real
    // do dispositivo para nunca emprestar a temperatura de outro disco.
    if (QRegularExpression(QStringLiteral("^sd[a-z]+$")).match(deviceName).hasMatch()) {
        const QString diskPath = QFileInfo(QDir(blockRoot).filePath(deviceName + QStringLiteral("/device"))).canonicalFilePath();
        if (diskPath.isEmpty()) return {};
        for (const QString &node : monitorDirs(hwmonRoot)) {
            const QString dir = QDir(hwmonRoot).filePath(node);
            if (readText(dir + QStringLiteral("/name")) != QStringLiteral("drivetemp")) continue;
            const QString hwPath = QFileInfo(dir).canonicalFilePath();
            const QString hwDevice = QFileInfo(dir + QStringLiteral("/device")).canonicalFilePath();
            if (!atOrUnder(hwPath, diskPath) && !atOrUnder(hwDevice, diskPath)) continue;
            const auto reading = input(dir + QStringLiteral("/temp1_input"),
                QStringLiteral("drivetemp / ") + deviceName);
            if (reading.valid()) return reading;
        }
        return {};
    }

    // NVMe: somente sensores vinculados AO controlador correto.
    const auto match = QRegularExpression(QStringLiteral("^(nvme[0-9]+)n[0-9]+$")).match(deviceName);
    if (!match.hasMatch()) return {};
    const QString controller = match.captured(1);
    const QString controllerPath = QFileInfo(QDir(nvmeRoot).filePath(controller)).canonicalFilePath();
    const QString pciPath = QFileInfo(QDir(nvmeRoot).filePath(controller + QStringLiteral("/device"))).canonicalFilePath();
    if (controllerPath.isEmpty()) return {};
    for (const QString &node : monitorDirs(hwmonRoot)) {
        const QString dir = QDir(hwmonRoot).filePath(node);
        if (readText(dir + QStringLiteral("/name")).compare(QStringLiteral("nvme"), Qt::CaseInsensitive) != 0)
            continue;
        const QString hwPath = QFileInfo(dir).canonicalFilePath();
        const QString hwDevice = QFileInfo(dir + QStringLiteral("/device")).canonicalFilePath();
        const bool belongsToController = atOrUnder(hwPath, controllerPath) ||
                                         atOrUnder(hwDevice, controllerPath) ||
                                         (!pciPath.isEmpty() && (hwPath.startsWith(pciPath + QStringLiteral("/nvme/") + controller + QLatin1Char('/')) ||
                                                                  hwDevice == pciPath));
        if (!belongsToController) continue;
        const QStringList labels = QDir(dir).entryList({QStringLiteral("temp*_input")}, QDir::Files, QDir::Name);
        for (const QString &sensor : labels) {
            const QString label = readText(dir + QLatin1Char('/') + sensor.left(sensor.size() - 5) + QStringLiteral("label"));
            // Sensor NVMe "Composite" preferido; ignorar sensores individuais quando há um composto.
            if (sensor != QStringLiteral("temp1_input") &&
                !label.contains(QStringLiteral("Composite"), Qt::CaseInsensitive)) continue;
            const auto result = input(dir + QLatin1Char('/') + sensor,
                QStringLiteral("NVMe ") + controller + (label.isEmpty() ? QString{} : QStringLiteral(" / ") + label));
            if (result.valid()) return result;
        }
    }
    return {};
}
}
