#include "legion/backends/lenovo/lenovo_telemetry.hpp"
#include "legion/backends/linux/linux_telemetry.hpp"
#include "legion/backends/nvidia/nvidia_telemetry.hpp"
#include "legion/core/interfaces.hpp"
#include "legion/core/types.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    bool json_mode = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--json") {
            json_mode = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: legion-cli [--json]\n"
                      << "Read-only hardware discovery and telemetry CLI.\n";
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return 2;
        }
    }

    auto sysfs = legion::core::create_posix_sysfs_reader();
    auto runner = legion::core::create_posix_process_runner();

    legion::backends::linux_sysfs::LinuxTelemetryProvider linux_provider(*sysfs);
    legion::backends::lenovo::LenovoTelemetryProvider lenovo_provider(*sysfs);
    legion::backends::nvidia::NvidiaTelemetryProvider nvidia_provider(*runner);

    auto snapshot = linux_provider.collect_system_metadata();
    linux_provider.collect(snapshot.capabilities, snapshot.readings);
    lenovo_provider.collect(snapshot.capabilities, snapshot.readings);
    nvidia_provider.collect(snapshot.capabilities, snapshot.readings);

    if (json_mode) {
        std::cout << legion::core::to_json(snapshot);
        return 0;
    }

    std::cout << "Machine: " << snapshot.product_version
              << " (" << snapshot.product_name << "), BIOS " << snapshot.bios_version << "\n";
    std::cout << "Kernel:  " << snapshot.kernel_version << "\n";
    std::cout << "Time:    " << snapshot.timestamp_iso8601 << "\n\n";

    std::cout << "Capabilities:\n";
    for (const auto& c : snapshot.capabilities) {
        std::cout << "  - [" << legion::core::to_string(c.state) << "] "
                  << c.name << " (" << c.backend << ")";
        if (!c.notes.empty()) std::cout << " - " << c.notes;
        std::cout << "\n";
    }

    std::cout << "\nReadings:\n";
    for (const auto& r : snapshot.readings) {
        std::cout << "  - " << r.label << ": ";
        if (r.is_numeric) {
            std::cout << r.numeric_value;
            auto u = legion::core::to_string(r.unit);
            if (!u.empty()) std::cout << " " << u;
        } else {
            std::cout << r.text_value;
        }
        std::cout << " [" << legion::core::to_string(r.state) << "]\n";
    }

    return 0;
}
