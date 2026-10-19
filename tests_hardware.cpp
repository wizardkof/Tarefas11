#include "HardwareMetadata.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QDebug>
#include <cmath>

namespace {
bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QByteArray memory = R"(
MEMORY_ARRAY_NUM_DEVICES=4
MEMORY_DEVICE_0_PRESENT=0
MEMORY_DEVICE_1_SIZE=34359738368
MEMORY_DEVICE_1_TYPE=DDR5
MEMORY_DEVICE_1_CONFIGURED_SPEED_MTS=5200
MEMORY_DEVICE_1_SPEED_MTS=4800
MEMORY_DEVICE_1_MANUFACTURER=Corsair
MEMORY_DEVICE_2_PRESENT=0
MEMORY_DEVICE_3_SIZE=34359738368
MEMORY_DEVICE_3_TYPE=DDR5
MEMORY_DEVICE_3_CONFIGURED_SPEED_MTS=5200
MEMORY_DEVICE_3_MANUFACTURER=Corsair
)";
    const auto info = HardwareMetadata::parseMemoryUdev(memory);
    if (std::abs(info.installedGiB - 64.0) > 0.001 || info.modules != 2 || info.slotCount != 4 ||
        info.type != QStringLiteral("DDR5") || info.configuredSpeed != QStringLiteral("5200 MT/s (DMI)") ||
        info.slotsText != QStringLiteral("2 de 4")) {
        qCritical() << "Leitura de DIMMs incorreta" << info.installedGiB << info.slotsText;
        return 1;
    }

    QTemporaryDir temporary;
    if (!temporary.isValid()) return 2;
    const QDir dir(temporary.path());
    if (!dir.mkpath(QStringLiteral("hwmon0")) || !dir.mkpath(QStringLiteral("hwmon1"))) return 3;
    if (!writeFile(dir.filePath(QStringLiteral("hwmon0/name")), "spd5118\n") ||
        !writeFile(dir.filePath(QStringLiteral("hwmon0/temp1_input")), "42375\n") ||
        !writeFile(dir.filePath(QStringLiteral("hwmon1/name")), "amdgpu\n") ||
        !writeFile(dir.filePath(QStringLiteral("hwmon1/temp1_input")), "78500\n")) return 4;
    const auto temperature = HardwareMetadata::ramTemperatures(dir.path());
    if (temperature.sensorCount != 1 || !temperature.display.contains(QStringLiteral("42.4"))) {
        qCritical() << "Sensor GPU incluído incorretamente ou sensor SPD não detectado" << temperature.display;
        return 5;
    }

    const QByteArray vulkan = R"(
==========
GPU0:
    apiVersion    = 1.4.309
    vendorID      = 0x10de
    deviceID      = 0x2504
    deviceType    = PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
    deviceName    = NVIDIA GeForce RTX 3060
    driverName    = NVIDIA
GPU1:
    apiVersion    = 1.4.300
    vendorID      = 0x1002
    deviceID      = 0x164e
    deviceType    = PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
    deviceName    = AMD Radeon Graphics
    driverName    = radv
GPU2:
    apiVersion    = 1.4.300
    vendorID      = 0x10005
    deviceID      = 0x0000
    deviceType    = PHYSICAL_DEVICE_TYPE_CPU
    deviceName    = llvmpipe
)";
    const auto gpus = HardwareMetadata::parseVulkanSummary(vulkan);
    if (gpus.size() != 3 ||
        !HardwareMetadata::vulkanMatchesDevice(gpus[0], QStringLiteral("0x10de"), QStringLiteral("0x2504")) ||
        HardwareMetadata::vulkanMatchesDevice(gpus[0], QStringLiteral("0x1002"), QStringLiteral("0x164e")) ||
        !HardwareMetadata::vulkanMatchesDevice(gpus[1], QStringLiteral("0x1002"), QStringLiteral("0x164e")) ||
        HardwareMetadata::vulkanMatchesDevice(gpus[2], QStringLiteral("0x10005"), QStringLiteral("0x0000"))) {
        qCritical() << "Associação Vulkan->PCI incorreta" << gpus.size();
        return 6;
    }
    qInfo() << "DMI/udev, SPD e Vulkan/PCI PASS";
    return 0;
}
