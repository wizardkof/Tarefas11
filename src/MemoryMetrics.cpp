#include "MemoryMetrics.h"

#include <QLocale>
#include <cmath>

namespace MemoryMetrics {
ProcessMemory parseProcessStatus(const QByteArray &status)
{
    ProcessMemory result;
    for (const QByteArray &line : status.split('\n')) {
        const bool resident = line.startsWith("VmRSS:");
        const bool swap = line.startsWith("VmSwap:");
        if (!resident && !swap) continue;
        const auto tokens = line.mid(resident ? 6 : 7).simplified().split(' ');
        if (tokens.isEmpty()) continue;
        bool valid = false;
        const quint64 kib = tokens.constFirst().toULongLong(&valid);
        if (!valid) continue;
        const double mib = static_cast<double>(kib) / 1024.0;
        if (resident) {
            result.residentMiB = mib;
            result.residentAvailable = true;
        } else {
            result.swapMiB = mib;
            result.swapAvailable = true;
        }
    }
    return result;
}

QString formatMiB(double valueMiB)
{
    if (!std::isfinite(valueMiB) || valueMiB < 0) return QStringLiteral("—");
    const double megabytes = valueMiB * 1048576.0 / 1000000.0;
    const QLocale locale(QLocale::Portuguese, QLocale::Brazil);
    if (megabytes < 1000.0 - 1e-7)
        return locale.toString(megabytes, 'f', 1) + QStringLiteral(" MB");
    return locale.toString(megabytes / 1000.0, 'f', 2) + QStringLiteral(" GB");
}
}
