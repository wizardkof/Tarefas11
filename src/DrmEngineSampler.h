#pragma once
#include <QHash>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QString>

// Indicadores opcionais de engines a partir de /proc/PID/fdinfo DRM.
// Cobrem apenas clientes acessíveis ao usuário, não a utilização total da GPU.
class DrmEngineSampler final {
public:
    struct Engine { double render=-1, copy=-1, encode=-1, decode=-1; };
    DrmEngineSampler();
    void update();
    Engine device(const QString &pci) const;
private:
    using DeviceTotals = QHash<QString, quint64>; // render/copy/encode/decode, nanosegundos cumulativos
    using Totals = QHash<QString, DeviceTotals>; // chave PCI canonical
    struct Result { Totals total; QHash<QString, Engine> engines; bool valid = false; };
    static Result scan(const Totals &previous, double seconds, bool baseline);
    Totals m_previous;
    QHash<QString, Engine> m_engines;
    QElapsedTimer m_clock;
    QFutureWatcher<Result> m_watcher;
    bool m_baseline=false;
};
