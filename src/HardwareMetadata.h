#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace HardwareMetadata {

struct MemoryInfo {
    double installedGiB = -1.0;
    int modules = 0;
    int slotCount = 0;
    QString type;
    QString configuredSpeed;
    QString modulesText;
    QString slotsText;
};

// Dados DMI exportados pelo systemd-udevd. Não exige privilégios para ler.
MemoryInfo parseMemoryUdev(const QByteArray &properties);

struct RamTemperatures {
    QString display;  // "Não disponível" quando nenhum sensor SPD está exposto
    int sensorCount = 0;
};

// Só sensores físicos de módulos (SPD5118/JC42): não confundir com CPU/GPU.
RamTemperatures ramTemperatures(const QString &hwmonPath = QStringLiteral("/sys/class/hwmon"));

struct VulkanGpu {
    QString apiVersion;
    QString vendorId;
    QString deviceId;
    QString deviceName;
    QString driverName;
    QString deviceType;
};

QVector<VulkanGpu> parseVulkanSummary(const QByteArray &output);
bool vulkanMatchesDevice(const VulkanGpu &candidate, const QString &sysVendor, const QString &sysDevice);

} // namespace HardwareMetadata
