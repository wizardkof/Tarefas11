cmake_minimum_required(VERSION 3.21)
project(Tarefas11 VERSION 0.4.16 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Concurrent DBus Gui Network Qml Quick QuickControls2 Widgets)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(tarefas11
    src/main.cpp
    src/SystemMonitor.h
    src/SystemMonitor.cpp
    src/HardwareDevices.cpp
    src/HardwareMetadata.h
    src/HardwareMetadata.cpp
    src/TemperatureSensors.h
    src/TemperatureSensors.cpp
    src/SataSmart.h
    src/SataSmart.cpp
    src/DrmEngineSampler.h
    src/DrmEngineSampler.cpp
    src/TcpSampler.h
    src/TcpSampler.cpp
    src/ColumnPreferences.h
    src/ColumnPreferences.cpp
    src/MemoryMetrics.h
    src/MemoryMetrics.cpp
    src/ProcessModel.h
    src/ProcessModel.cpp
    src/ServicesModel.h
    src/ServicesModel.cpp
    src/UsersModel.h
    src/UsersModel.cpp
    src/StartupModel.h
    src/StartupModel.cpp
    src/AppIdentity.h
    src/AppIdentity.cpp
    src/PeIconExtractor.h
    src/PeIconExtractor.cpp
)

foreach(qml_file IN ITEMS Main.qml HistoryGraph.qml MetricTile.qml ProcessInfoPanel.qml MemoryDashboard.qml GpuDashboard.qml)
    set_source_files_properties(qml/${qml_file} PROPERTIES QT_RESOURCE_ALIAS ${qml_file})
endforeach()

qt_add_qml_module(tarefas11
    URI Tarefas11
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/HistoryGraph.qml
        qml/MetricTile.qml
        qml/ProcessInfoPanel.qml
        qml/MemoryDashboard.qml
    qml/GpuDashboard.qml
)

target_link_libraries(tarefas11 PRIVATE Qt6::Core Qt6::Concurrent Qt6::DBus Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Widgets)

include(CTest)
if(BUILD_TESTING)
    add_executable(tarefas11-model-tests tests_model.cpp src/StartupModel.cpp src/StartupModel.h)
    target_include_directories(tarefas11-model-tests PRIVATE src)
    target_link_libraries(tarefas11-model-tests PRIVATE Qt6::Core Qt6::Gui Qt6::Quick)
    add_test(NAME startup_xdg_kde COMMAND tarefas11-model-tests)
    add_executable(tarefas11-column-tests tests_columns.cpp src/ColumnPreferences.cpp src/ColumnPreferences.h)
    target_include_directories(tarefas11-column-tests PRIVATE src)
    target_link_libraries(tarefas11-column-tests PRIVATE Qt6::Core)
    add_test(NAME columns_persistence COMMAND tarefas11-column-tests)
    add_executable(tarefas11-hardware-tests tests_hardware.cpp src/HardwareMetadata.cpp src/HardwareMetadata.h src/TemperatureSensors.cpp src/TemperatureSensors.h)
    target_include_directories(tarefas11-hardware-tests PRIVATE src)
    target_link_libraries(tarefas11-hardware-tests PRIVATE Qt6::Core)
    add_test(NAME memory_spd_vulkan_mapping COMMAND tarefas11-hardware-tests)
    add_executable(tarefas11-temperature-tests tests_temperature.cpp src/TemperatureSensors.cpp src/TemperatureSensors.h)
    target_include_directories(tarefas11-temperature-tests PRIVATE src)
    target_link_libraries(tarefas11-temperature-tests PRIVATE Qt6::Core)
    add_test(NAME cpu_nvme_temperatures COMMAND tarefas11-temperature-tests)
    add_executable(tarefas11-sata-tests tests_sata.cpp src/SataSmart.cpp src/SataSmart.h)
    target_include_directories(tarefas11-sata-tests PRIVATE src)
    target_link_libraries(tarefas11-sata-tests PRIVATE Qt6::Core Qt6::DBus)
    add_test(NAME sata_smart_conversion COMMAND tarefas11-sata-tests)
    add_executable(tarefas11-memory-tests tests_memory.cpp src/MemoryMetrics.cpp src/MemoryMetrics.h)
    target_include_directories(tarefas11-memory-tests PRIVATE src)
    target_link_libraries(tarefas11-memory-tests PRIVATE Qt6::Core)
    add_test(NAME process_ram_swap_units COMMAND tarefas11-memory-tests)
endif()

install(TARGETS tarefas11 RUNTIME DESTINATION bin)
install(FILES assets/tarefas11.desktop DESTINATION share/applications)
install(FILES assets/tarefas11.svg DESTINATION share/icons/hicolor/scalable/apps)
