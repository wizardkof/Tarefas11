#include "SataSmart.h"
#include <QCoreApplication>
#include <QDebug>
#include <cmath>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (SataSmart::blockPath(QStringLiteral("sda")) != QStringLiteral("/org/freedesktop/UDisks2/block_devices/sda") ||
        !SataSmart::blockPath(QStringLiteral("sda1")).isEmpty() ||
        !SataSmart::blockPath(QStringLiteral("nvme0n1")).isEmpty() ||
        !SataSmart::blockPath(QStringLiteral("../../danger")).isEmpty()) return 1;
    const double valid = SataSmart::celsiusFromKelvin(315.4, 123456789);
    if (std::abs(valid - 42.25) > .001) return 2;
    if (!std::isnan(SataSmart::celsiusFromKelvin(0, 123456789)) ||
        !std::isnan(SataSmart::celsiusFromKelvin(310.0, 0)) ||
        !std::isnan(SataSmart::celsiusFromKelvin(1000.0, 12345))) return 3;
    qInfo() << "Conversão Kelvin/SMART e nomes de bloco PASS";
    return 0;
}
