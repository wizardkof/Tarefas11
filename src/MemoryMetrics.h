#pragma once

#include <QByteArray>
#include <QString>

// Linux /proc/<pid>/status: VmRSS = RAM residente; VmSwap = swap privada
// anônima (não inclui shmem/toda a swap de um grupo). Valores em kB no kernel.
namespace MemoryMetrics {
struct ProcessMemory {
    double residentMiB = 0;
    double swapMiB = 0;
    bool residentAvailable = false;
    bool swapAvailable = false;
};

ProcessMemory parseProcessStatus(const QByteArray &status);
// Entrada binária em MiB, saída decimal com unidades MB/GB para a interface.
QString formatMiB(double valueMiB);
}
