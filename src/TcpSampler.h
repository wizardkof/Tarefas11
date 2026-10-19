#pragma once
#include <QElapsedTimer>
#include <QHash>
#include <QFutureWatcher>
#include <QVector>
#include <QtGlobal>

// Estimativas TCP, não um contador completo de rede por PID.
// Varredura assíncrona para não bloquear a interface QML com milhares de FDs.
class TcpSampler final {
public:
    struct Rate { double rxMiBs = 0, txMiBs = 0; bool valid = false; };
    TcpSampler();
    void update(const QVector<int> &pids);
    Rate rateForPid(int pid) const { return m_pidRate.value(pid); }
private:
    struct Bytes { quint64 rx = 0, tx = 0; };
    struct Result {
        QHash<quint64, Bytes> sockets;
        QHash<int, Rate> rates;
        bool succeeded = false;
    };
    static bool dumpFamily(int family, QHash<quint64, Bytes> *out);
    static Result scan(const QVector<int> &pids, const QHash<quint64, Bytes> &previous,
                       double seconds, bool baseline);
    QHash<quint64, Bytes> m_prevSockets;
    QHash<int, Rate> m_pidRate;
    QElapsedTimer m_sinceLast;
    QFutureWatcher<Result> m_watcher;
    bool m_baseline = false;
};
