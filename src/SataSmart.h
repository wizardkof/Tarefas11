#pragma once

#include "TemperatureSensors.h"
#include <QHash>
#include <QStringList>

// Somente leitura da temperatura ATA S.M.A.R.T. publicada por UDisks2.
// sample() DEVE ser executado em worker (D-Bus síncrono com timeout).
namespace SataSmart {
    double celsiusFromKelvin(double kelvin, quint64 lastUpdateUnix);
    QString blockPath(const QString &deviceName);
    QHash<QString, TemperatureSensors::Reading> sample(const QStringList &deviceNames);
}
