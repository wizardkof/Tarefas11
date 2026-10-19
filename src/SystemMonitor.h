#pragma once

#include <QObject>
#include "DrmEngineSampler.h"
#include "TemperatureSensors.h"
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QHash>
#include <QMap>
#include <QVariantMap>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QVector>

class SystemMonitor final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY updated)
    Q_PROPERTY(double diskBusyPercent READ diskBusyPercent NOTIFY updated)
    Q_PROPERTY(double cpuTemperatureC READ cpuTemperatureC NOTIFY updated)
    Q_PROPERTY(qulonglong systemOpenFiles READ systemOpenFiles NOTIFY updated)
    Q_PROPERTY(double memoryInstalledGiB READ memoryInstalledGiB NOTIFY updated)
    Q_PROPERTY(double memoryBuffersGiB READ memoryBuffersGiB NOTIFY updated)
    Q_PROPERTY(double memoryFreeGiB READ memoryFreeGiB NOTIFY updated)
    Q_PROPERTY(double memorySlabGiB READ memorySlabGiB NOTIFY updated)
    Q_PROPERTY(double memorySharedGiB READ memorySharedGiB NOTIFY updated)
    Q_PROPERTY(double memoryCommittedGiB READ memoryCommittedGiB NOTIFY updated)
    Q_PROPERTY(double memoryActiveGiB READ memoryActiveGiB NOTIFY updated)
    Q_PROPERTY(double zramOriginalGiB READ zramOriginalGiB NOTIFY updated)
    Q_PROPERTY(double zramCompressedGiB READ zramCompressedGiB NOTIFY updated)
    Q_PROPERTY(double zramPhysicalGiB READ zramPhysicalGiB NOTIFY updated)
    Q_PROPERTY(QString memoryType READ memoryType NOTIFY updated)
    Q_PROPERTY(QString memorySpeed READ memorySpeed NOTIFY updated)
    Q_PROPERTY(QString memorySlots READ memorySlots NOTIFY updated)
    Q_PROPERTY(QString memoryTemperature READ memoryTemperature NOTIFY updated)
    Q_PROPERTY(int memoryTemperatureSensors READ memoryTemperatureSensors NOTIFY updated)
    Q_PROPERTY(QString cpuCacheL1 READ cpuCacheL1 CONSTANT)
    Q_PROPERTY(QString cpuCacheL2 READ cpuCacheL2 CONSTANT)
    Q_PROPERTY(QString cpuCacheL3 READ cpuCacheL3 CONSTANT)
    Q_PROPERTY(int cpuSockets READ cpuSockets CONSTANT)
    Q_PROPERTY(QString cpuArchitecture READ cpuArchitecture CONSTANT)
    Q_PROPERTY(double cpuBaseGHz READ cpuBaseGHz CONSTANT)
    Q_PROPERTY(double cpuMaxGHz READ cpuMaxGHz CONSTANT)
    Q_PROPERTY(QString cpuVirtualization READ cpuVirtualization CONSTANT)
    Q_PROPERTY(QString graphicsApiInfo READ graphicsApiInfo NOTIFY updated)
    Q_PROPERTY(QString openGlInfo READ openGlInfo NOTIFY updated)
    Q_PROPERTY(QString memoryModules READ memoryModules NOTIFY updated)
    Q_PROPERTY(double cpuPercent READ cpuPercent NOTIFY updated)
    Q_PROPERTY(double cpuFrequencyGHz READ cpuFrequencyGHz NOTIFY updated)
    Q_PROPERTY(int physicalCoreCount READ physicalCoreCount CONSTANT)
    Q_PROPERTY(double memoryCachedGiB READ memoryCachedGiB NOTIFY updated)
    Q_PROPERTY(double memoryAvailableGiB READ memoryAvailableGiB NOTIFY updated)
    Q_PROPERTY(QString gpuDriver READ gpuDriver NOTIFY updated)
    Q_PROPERTY(double memoryPercent READ memoryPercent NOTIFY updated)
    Q_PROPERTY(double memoryUsedGiB READ memoryUsedGiB NOTIFY updated)
    Q_PROPERTY(double memoryTotalGiB READ memoryTotalGiB NOTIFY updated)
    Q_PROPERTY(double swapUsedGiB READ swapUsedGiB NOTIFY updated)
    Q_PROPERTY(double swapTotalGiB READ swapTotalGiB NOTIFY updated)
    Q_PROPERTY(double diskReadMBs READ diskReadMBs NOTIFY updated)
    Q_PROPERTY(double diskWriteMBs READ diskWriteMBs NOTIFY updated)
    Q_PROPERTY(double netRxMBs READ netRxMBs NOTIFY updated)
    Q_PROPERTY(double netTxMBs READ netTxMBs NOTIFY updated)
    Q_PROPERTY(double gpuPercent READ gpuPercent NOTIFY updated)
    Q_PROPERTY(double gpuMemoryUsedMiB READ gpuMemoryUsedMiB NOTIFY updated)
    Q_PROPERTY(double gpuMemoryTotalMiB READ gpuMemoryTotalMiB NOTIFY updated)
    Q_PROPERTY(int gpuTemperatureC READ gpuTemperatureC NOTIFY updated)
    Q_PROPERTY(bool gpuAvailable READ gpuAvailable NOTIFY updated)
    Q_PROPERTY(QString gpuName READ gpuName NOTIFY updated)
    Q_PROPERTY(QString cpuName READ cpuName CONSTANT)
    Q_PROPERTY(int coreCount READ coreCount NOTIFY updated)
    Q_PROPERTY(double uptimeSeconds READ uptimeSeconds NOTIFY updated)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY updated)
    Q_PROPERTY(QVariantList memoryHistory READ memoryHistory NOTIFY updated)
    Q_PROPERTY(QVariantList swapHistory READ swapHistory NOTIFY updated)
    Q_PROPERTY(QVariantList diskHistory READ diskHistory NOTIFY updated)
    Q_PROPERTY(QVariantList networkHistory READ networkHistory NOTIFY updated)
    Q_PROPERTY(QVariantList gpuHistory READ gpuHistory NOTIFY updated)
    Q_PROPERTY(QVariantList perCoreHistories READ perCoreHistories NOTIFY updated)
    Q_PROPERTY(int intervalMs READ intervalMs WRITE setIntervalMs NOTIFY intervalChanged)

public:
    QVariantList devices() const;
    Q_INVOKABLE QVariantMap deviceInfo(const QString &key) const;
    Q_INVOKABLE QVariantList historyFor(const QString &key) const;
    double diskBusyPercent() const { return m_diskBusyPercent; }
    double cpuTemperatureC() const { return m_cpuTemperatureC; }
    qulonglong systemOpenFiles() const { return m_systemOpenFiles; }
    double memoryInstalledGiB() const { return m_memoryInstalledGiB; }
    double memoryBuffersGiB() const { return m_memoryBuffersGiB; }
    double memoryFreeGiB() const { return m_memoryFreeGiB; }
    double memorySlabGiB() const { return m_memorySlabGiB; }
    double memorySharedGiB() const { return m_memorySharedGiB; }
    double memoryCommittedGiB() const { return m_memoryCommittedGiB; }
    double memoryActiveGiB() const { return m_memoryActiveGiB; }
    double zramOriginalGiB() const { return m_zramOriginalGiB; }
    double zramCompressedGiB() const { return m_zramCompressedGiB; }
    double zramPhysicalGiB() const { return m_zramPhysicalGiB; }
    QString memoryType() const { return m_memoryType; }
    QString memorySpeed() const { return m_memorySpeed; }
    QString memorySlots() const { return m_memorySlots; }
    QString memoryTemperature() const { return m_memoryTemperature; }
    int memoryTemperatureSensors() const { return m_memoryTemperatureSensors; }
    QString cpuCacheL1() const { return m_cacheL1; }
    QString cpuCacheL2() const { return m_cacheL2; }
    QString cpuCacheL3() const { return m_cacheL3; }
    int cpuSockets() const { return m_cpuSockets; }
    QString cpuArchitecture() const { return m_architecture; }
    double cpuBaseGHz() const { return m_cpuBaseGHz; }
    double cpuMaxGHz() const { return m_cpuMaxGHz; }
    QString cpuVirtualization() const { return m_virtualization; }
    QString graphicsApiInfo() const { return m_graphicsApiInfo; }
    QString openGlInfo() const { return m_openGlInfo; }
    QString memoryModules() const { return m_memoryModules; }
    Q_INVOKABLE void requestMemoryHardwareDetails();
    explicit SystemMonitor(QObject *parent = nullptr);
    void start();
    void primeSnapshot();
    int intervalMs() const { return m_intervalMs; }
    void setIntervalMs(int value);

    double cpuPercent() const { return m_cpuPercent; }
    double cpuFrequencyGHz() const { return m_cpuFrequencyGHz; }
    int physicalCoreCount() const { return m_physicalCores; }
    double memoryCachedGiB() const { return m_memoryCachedGiB; }
    double memoryAvailableGiB() const { return m_memoryAvailableGiB; }
    QString gpuDriver() const { return m_gpuDriver; }
    double memoryPercent() const { return m_memoryPercent; }
    double memoryUsedGiB() const { return m_memoryUsedGiB; }
    double memoryTotalGiB() const { return m_memoryTotalGiB; }
    double swapUsedGiB() const { return m_swapUsedGiB; }
    double swapTotalGiB() const { return m_swapTotalGiB; }
    double diskReadMBs() const { return m_diskReadMBs; }
    double diskWriteMBs() const { return m_diskWriteMBs; }
    double netRxMBs() const { return m_netRxMBs; }
    double netTxMBs() const { return m_netTxMBs; }
    double gpuPercent() const { return m_gpuPercent; }
    double gpuMemoryUsedMiB() const { return m_gpuMemoryUsedMiB; }
    double gpuMemoryTotalMiB() const { return m_gpuMemoryTotalMiB; }
    int gpuTemperatureC() const { return m_gpuTemperatureC; }
    bool gpuAvailable() const { return m_gpuAvailable; }
    QString gpuName() const { return m_gpuName; }
    QString cpuName() const { return m_cpuName; }
    int coreCount() const { return m_coreHistories.size(); }
    double uptimeSeconds() const { return m_uptimeSeconds; }

    QVariantList cpuHistory() const;
    QVariantList memoryHistory() const;
    QVariantList swapHistory() const;
    QVariantList diskHistory() const;
    QVariantList networkHistory() const;
    QVariantList gpuHistory() const;
    QVariantList perCoreHistories() const;

signals:
    void updated();
    void intervalChanged();
    void sampled(quint64 totalCpuTicks);

private:
    struct CpuTicks {
        quint64 total = 0;
        quint64 idle = 0;
    };
    void poll();
    void sampleMemory();
    void pollGpu();
    void enumerateGpus();
    void sampleGpus();
    void sampleDisks(double seconds);
    void sampleNetworks(double seconds);
    void pollGraphicsApi();
    void loadMemoryUdev();
    void pollNetworkNames();
    void upsertDevice(const QString &key, const QVariantMap &attributes);
    static void appendHistory(QVector<double> &series, double value);
    static QVariantList list(const QVector<double> &values);

    QTimer m_timer;
    QTimer m_gpuTimer;
    QProcess m_gpuProcess;
    QProcess m_gpuEngineProcess;
    QProcess m_apiProcess;
    QProcess m_glProcess;
    QProcess m_dmiProcess;
    QProcess m_udevProcess;
    QElapsedTimer m_ramTempTimer;
    QElapsedTimer m_temperatureTimer;
    QElapsedTimer m_sataSmartTimer;
    QFutureWatcher<QHash<QString, TemperatureSensors::Reading>> m_sataSmartWatcher;
    QHash<QString, TemperatureSensors::Reading> m_sataSmartReadings;
    QProcess m_nmProcess;
    QTimer m_nmTimer;
    QElapsedTimer m_elapsed;
    int m_intervalMs = 1500;
    CpuTicks m_lastCpu;
    QVector<CpuTicks> m_lastCores;
    QVector<QVector<double>> m_coreHistories;
    quint64 m_lastDiskRead = 0;
    quint64 m_lastDiskWrite = 0;
    quint64 m_lastNetworkRx = 0;
    quint64 m_lastNetworkTx = 0;
    bool m_haveDiskBaseline = false;
    bool m_haveNetworkBaseline = false;
    QString m_cpuName;
    QString m_gpuName = QStringLiteral("GPU NVIDIA");
    QString m_gpuDriver;
    double m_cpuPercent = 0;
    double m_cpuFrequencyGHz = 0;
    double m_memoryCachedGiB = 0;
    double m_memoryAvailableGiB = 0;
    int m_physicalCores = 0;
    double m_memoryPercent = 0;
    double m_memoryUsedGiB = 0;
    double m_memoryTotalGiB = 0;
    double m_swapUsedGiB = 0;
    double m_swapTotalGiB = 0;
    double m_diskReadMBs = 0;
    double m_diskWriteMBs = 0;
    double m_netRxMBs = 0;
    double m_netTxMBs = 0;
    double m_gpuPercent = 0;
    double m_gpuMemoryUsedMiB = 0;
    double m_gpuMemoryTotalMiB = 0;
    double m_uptimeSeconds = 0;
    int m_gpuTemperatureC = 0;
    bool m_gpuAvailable = false;
    DrmEngineSampler m_drmEngines;
    QMap<QString, QVariantMap> m_deviceInfo;
    QHash<QString, QVector<double>> m_deviceHistories;
    QHash<QString, quint64> m_diskLastRead, m_diskLastWrite, m_diskLastBusy;
    QHash<QString, quint64> m_diskLastReadIos, m_diskLastWriteIos, m_diskLastIoMillis;
    QHash<QString, quint64> m_netLastRx, m_netLastTx;
    QHash<QString, double> m_netRateRx, m_netRateTx;
    QHash<QString, QString> m_gpuPciToKey;
    QHash<QString, QString> m_connections;
    double m_diskBusyPercent = 0;
    double m_cpuTemperatureC = -1.0;
    qulonglong m_systemOpenFiles = 0;
    double m_memoryInstalledGiB = -1.0; // udev/DMI primeiro; SMBIOS autorizado como alternativa.
    double m_memoryBuffersGiB = 0, m_memoryFreeGiB = 0, m_memorySlabGiB = 0;
    double m_memorySharedGiB = 0, m_memoryCommittedGiB = 0, m_memoryActiveGiB = 0;
    double m_zramOriginalGiB = 0, m_zramCompressedGiB = 0, m_zramPhysicalGiB = 0;
    QString m_memoryType = QStringLiteral("Não disponível (udev/SMBIOS)");
    QString m_memorySpeed = QStringLiteral("Não disponível (udev/SMBIOS)");
    QString m_memorySlots = QStringLiteral("Não disponível");
    QString m_memoryTemperature = QStringLiteral("Indisponível (sensor SPD)");
    int m_memoryTemperatureSensors = 0;
    QString m_cacheL1 = QStringLiteral("—"), m_cacheL2 = QStringLiteral("—"), m_cacheL3 = QStringLiteral("—");
    QString m_openGlInfo = QStringLiteral("Indisponível (glxinfo)");
    QString m_memoryModules = QStringLiteral("Aguardando DMI/udev (sem privilégios)");
    QString m_architecture, m_virtualization = QStringLiteral("—"), m_graphicsApiInfo = QStringLiteral("Não detectadas");
    int m_cpuSockets = 0;
    double m_cpuBaseGHz = 0, m_cpuMaxGHz = 0;
    QVector<double> m_cpuHistory;
    QVector<double> m_memoryHistory;
    QVector<double> m_swapHistory;
    QVector<double> m_diskHistory;
    QVector<double> m_networkHistory;
    QVector<double> m_gpuHistory;
};
