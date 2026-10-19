#pragma once

#include <QString>

// Temperaturas publicadas pelo kernel em sysfs/hwmon, sem sudo e sem polling SMART.
// Não inferir temperaturas quando o sensor não estiver presente ou legível.
namespace TemperatureSensors {
struct Reading {
    double celsius = -1.0;
    QString source;
    // Leitura ausente: source vazio. O sentinela -1 C sozinho não basta
    // porque -1 C é uma temperatura fisicamente possível.
    bool valid() const { return !source.isEmpty() && celsius >= -10.0 && celsius <= 125.0; }
};

Reading cpu(const QString &hwmonRoot = QStringLiteral("/sys/class/hwmon"));
Reading disk(const QString &deviceName,
             const QString &hwmonRoot = QStringLiteral("/sys/class/hwmon"),
             const QString &nvmeRoot = QStringLiteral("/sys/class/nvme"),
             const QString &blockRoot = QStringLiteral("/sys/class/block"));
}
