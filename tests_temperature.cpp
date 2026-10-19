#include "TemperatureSensors.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QDebug>
#include <cmath>
#include <utility>
#include <unistd.h>

namespace {
bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const QString root = temporary.path();
    const QString hwmon = root + QStringLiteral("/hwmon");
    const QString nvme = root + QStringLiteral("/nvme");
    QDir dir;
    if (!dir.mkpath(hwmon + QStringLiteral("/hwmon0")) ||
        !dir.mkpath(hwmon + QStringLiteral("/hwmon1")) ||
        !dir.mkpath(nvme + QStringLiteral("/nvme0/hwmon/hwmon7")) ||
        !dir.mkpath(nvme + QStringLiteral("/nvme1/hwmon/hwmon8"))) return 2;
    if (!writeFile(hwmon + QStringLiteral("/hwmon0/name"), "k10temp\n") ||
        !writeFile(hwmon + QStringLiteral("/hwmon0/temp1_input"), "60000\n") ||
        !writeFile(hwmon + QStringLiteral("/hwmon0/temp1_label"), "Tctl\n") ||
        !writeFile(hwmon + QStringLiteral("/hwmon0/temp2_input"), "53500\n") ||
        !writeFile(hwmon + QStringLiteral("/hwmon0/temp2_label"), "Tdie\n") ||
        !writeFile(hwmon + QStringLiteral("/hwmon1/name"), "amdgpu\n") ||
        !writeFile(hwmon + QStringLiteral("/hwmon1/temp1_input"), "98000\n")) return 3;
    // Regressão: ausência de sensor não pode ser interpretada como -1 C válido.
    if (TemperatureSensors::Reading{}.valid() ||
        TemperatureSensors::cpu(root + QStringLiteral("/hwmon-inexistente")).valid()) {
        qCritical() << "Ausência de sensor reportada incorretamente como temperatura válida";
        return 8;
    }
    const auto reading = TemperatureSensors::cpu(hwmon);
    if (!reading.valid() || std::abs(reading.celsius - 53.5) > .01 ||
        !reading.source.contains(QStringLiteral("Tdie"))) {
        qCritical() << "CPU não priorizou Tdie" << reading.celsius << reading.source;
        return 4;
    }
    const QString d0 = nvme + QStringLiteral("/nvme0/hwmon/hwmon7");
    const QString d1 = nvme + QStringLiteral("/nvme1/hwmon/hwmon8");
    if (!writeFile(d0 + QStringLiteral("/name"), "nvme\n") ||
        !writeFile(d0 + QStringLiteral("/temp1_input"), "42250\n") ||
        !writeFile(d0 + QStringLiteral("/temp1_label"), "Composite\n") ||
        !writeFile(d1 + QStringLiteral("/name"), "nvme\n") ||
        !writeFile(d1 + QStringLiteral("/temp1_input"), "59125\n")) return 5;
    const QByteArray d0Bytes = QFile::encodeName(d0), d1Bytes = QFile::encodeName(d1);
    const QByteArray l0 = QFile::encodeName(hwmon + QStringLiteral("/hwmon7"));
    const QByteArray l1 = QFile::encodeName(hwmon + QStringLiteral("/hwmon8"));
    if (::symlink(d0Bytes.constData(), l0.constData()) != 0 ||
        ::symlink(d1Bytes.constData(), l1.constData()) != 0) return 6;
    const auto temp0 = TemperatureSensors::disk(QStringLiteral("nvme0n1"), hwmon, nvme);
    const auto temp1 = TemperatureSensors::disk(QStringLiteral("nvme1n1"), hwmon, nvme);
    const auto sata = TemperatureSensors::disk(QStringLiteral("sda"), hwmon, nvme);
    if (!temp0.valid() || !temp1.valid() || sata.valid() ||
        std::abs(temp0.celsius - 42.25) > .01 || std::abs(temp1.celsius - 59.125) > .01) {
        qCritical() << "Mapeamento de temperaturas NVMe incorreto" << temp0.celsius << temp1.celsius;
        return 7;
    }
    // Testar drivetemp com duas unidades SATA e um sensor sem disco: não
    // atribuir a temperatura do SSD A ao SSD B nem a um SSD sem sensor.
    const QString block = root + QStringLiteral("/block");
    const QString drives = root + QStringLiteral("/devices");
    const QString driveA = drives + QStringLiteral("/sata-A");
    const QString driveB = drives + QStringLiteral("/sata-B");
    if (!dir.mkpath(block + QStringLiteral("/sda")) ||
        !dir.mkpath(block + QStringLiteral("/sdb")) ||
        !dir.mkpath(drives + QStringLiteral("/sata-A/hwmon/hwmon10")) ||
        !dir.mkpath(drives + QStringLiteral("/sata-B/hwmon/hwmon11"))) return 9;
    for (const auto &[source, target] : {
            std::pair<QString, QString>{driveA, block + QStringLiteral("/sda/device")},
            std::pair<QString, QString>{driveB, block + QStringLiteral("/sdb/device")},
            std::pair<QString, QString>{driveA + QStringLiteral("/hwmon/hwmon10"), hwmon + QStringLiteral("/hwmon10")},
            std::pair<QString, QString>{driveB + QStringLiteral("/hwmon/hwmon11"), hwmon + QStringLiteral("/hwmon11")}}) {
        if (::symlink(QFile::encodeName(source).constData(), QFile::encodeName(target).constData()) != 0) return 10;
    }
    if (!writeFile(driveA + QStringLiteral("/hwmon/hwmon10/name"), "drivetemp\n") ||
        !writeFile(driveA + QStringLiteral("/hwmon/hwmon10/temp1_input"), "41125\n") ||
        !writeFile(driveB + QStringLiteral("/hwmon/hwmon11/name"), "drivetemp\n") ||
        !writeFile(driveB + QStringLiteral("/hwmon/hwmon11/temp1_input"), "57375\n")) return 11;
    const auto sataA = TemperatureSensors::disk(QStringLiteral("sda"), hwmon, nvme, block);
    const auto sataB = TemperatureSensors::disk(QStringLiteral("sdb"), hwmon, nvme, block);
    const auto sataMissing = TemperatureSensors::disk(QStringLiteral("sdc"), hwmon, nvme, block);
    if (!sataA.valid() || !sataB.valid() || sataMissing.valid() ||
        std::abs(sataA.celsius - 41.125) > .001 || std::abs(sataB.celsius - 57.375) > .001) {
        qCritical() << "Mapeamento drivetemp SATA incorreto" << sataA.celsius << sataB.celsius;
        return 12;
    }
    qInfo() << "CPU, NVMe e SATA drivetemp por dispositivo PASS";
    return 0;
}
