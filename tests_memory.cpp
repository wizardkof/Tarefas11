#include "MemoryMetrics.h"

#include <QCoreApplication>
#include <QDebug>
#include <QString>
#include <cmath>

static bool check(bool ok, const char *description) {
    if (!ok) qCritical() << "FALHOU:" << description;
    return ok;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QByteArray fixture =
        "Name:\tzen-bin\n"
        "VmSize:\t5632400 kB\n"
        "VmRSS:\t1258291 kB\n"
        "RssShmem:\t2048 kB\n"
        "VmSwap:\t2048000 kB\n";
    const auto m = MemoryMetrics::parseProcessStatus(fixture);
    if (!check(m.residentAvailable && m.swapAvailable, "RSS e SWAP presentes")) return 1;
    if (!check(std::abs(m.residentMiB - 1228.7998046875) < .01, "RSS em MiB")) return 1;
    if (!check(std::abs(m.swapMiB - 2000.0) < .01, "SWAP em MiB")) return 1;

    const auto noSwap = MemoryMetrics::parseProcessStatus("VmRSS:\t200 kB\n");
    if (!check(noSwap.residentAvailable && !noSwap.swapAvailable, "SWAP ausente nao deve virar zero")) return 1;
    const auto zeroSwap = MemoryMetrics::parseProcessStatus("VmRSS: 4096 kB\nVmSwap: 0 kB\n");
    if (!check(zeroSwap.swapAvailable && zeroSwap.swapMiB == 0, "SWAP zero explicita")) return 1;
    const auto malformed = MemoryMetrics::parseProcessStatus("VmSwap: erro kB\n");
    if (!check(!malformed.swapAvailable, "SWAP invalida indisponivel")) return 1;

    const QString mb = MemoryMetrics::formatMiB(999.0 * 1000000.0 / 1048576.0);
    const QString gb = MemoryMetrics::formatMiB(1000.0 * 1000000.0 / 1048576.0);
    if (!check(mb.endsWith(QStringLiteral(" MB")) && mb.startsWith(QStringLiteral("999")), "999 MB")) return 1;
    if (!check(gb == QStringLiteral("1,00 GB"), "limiar de 1 GB decimal")) return 1;
    if (!check(MemoryMetrics::formatMiB(2048.0).endsWith(QStringLiteral(" GB")), "acima de 1GB")) return 1;
    if (!check(MemoryMetrics::formatMiB(-1) == QStringLiteral("—"), "valor invalido")) return 1;
    return 0;
}
