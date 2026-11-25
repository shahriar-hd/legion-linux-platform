#include "legion/backends/linux/linux_telemetry.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <sys/statvfs.h>

namespace legion::backends::linux_sysfs {

namespace {

std::optional<double> parse_double(const std::optional<std::string>& text) {
    if (!text.has_value() || text->empty()) return std::nullopt;
    try {
        size_t idx = 0;
        double val = std::stod(*text, &idx);
        if (idx == 0) return std::nullopt;
        return val;
    } catch (...) {
        return std::nullopt;
    }
}

double steady_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

LinuxTelemetryProvider::LinuxTelemetryProvider(const core::ISysfsReader& sysfs)
    : sysfs_(sysfs) {}

core::SystemSnapshot LinuxTelemetryProvider::collect_system_metadata() const {
    core::SystemSnapshot snap;
    snap.product_name = sysfs_.read_text("/sys/class/dmi/id/product_name").value_or("unknown");
    snap.product_version = sysfs_.read_text("/sys/class/dmi/id/product_version").value_or("unknown");
    snap.bios_version = sysfs_.read_text("/sys/class/dmi/id/bios_version").value_or("unknown");
    snap.kernel_version = sysfs_.read_text("/proc/sys/kernel/osrelease").value_or("unknown");
    snap.timestamp_iso8601 = core::current_timestamp_iso8601();
    return snap;
}

void LinuxTelemetryProvider::collect(std::vector<core::CapabilityInfo>& capabilities,
                                     std::vector<core::SensorReading>& readings) const {
    const std::string ts = core::current_timestamp_iso8601();

    // 1. Platform profile
    const std::string profile_path = "/sys/firmware/acpi/platform_profile";
    const std::string choices_path = "/sys/firmware/acpi/platform_profile_choices";
    if (sysfs_.exists(profile_path)) {
        auto current = sysfs_.read_text(profile_path);
        auto choices = sysfs_.read_text(choices_path);
        capabilities.push_back({
            "platform_profile",
            "ACPI Platform Profile",
            backend_name(),
            current.has_value() ? core::CapabilityState::Readable : core::CapabilityState::Detected,
            profile_path,
            choices.has_value() ? ("choices: " + *choices) : ""
        });
        if (current.has_value()) {
            readings.push_back({
                "platform_profile",
                "Platform Profile",
                profile_path,
                0.0,
                *current,
                false,
                core::SensorUnit::Text,
                core::CapabilityState::Readable,
                ts,
                ""
            });
        }
    } else {
        capabilities.push_back({
            "platform_profile",
            "ACPI Platform Profile",
            backend_name(),
            core::CapabilityState::Unsupported,
            profile_path,
            "sysfs node not present"
        });
    }

    // 2. HWMON sensors (coretemp, nvme, BAT0, etc.)
    auto hwmon_dirs = sysfs_.list_dir("/sys/class/hwmon");
    std::sort(hwmon_dirs.begin(), hwmon_dirs.end());
    bool found_hwmon = !hwmon_dirs.empty();
    capabilities.push_back({
        "hwmon_telemetry",
        "Linux hwmon Sensors",
        backend_name(),
        found_hwmon ? core::CapabilityState::Readable : core::CapabilityState::Unsupported,
        "/sys/class/hwmon",
        found_hwmon ? (std::to_string(hwmon_dirs.size()) + " hwmon devices") : "no hwmon devices"
    });

    for (const auto& dir : hwmon_dirs) {
        const std::string base = "/sys/class/hwmon/" + dir;
        const std::string name = sysfs_.read_text(base + "/name").value_or(dir);
        auto files = sysfs_.list_dir(base);
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            if (f.rfind("temp", 0) == 0 && f.size() > 6 && f.substr(f.size() - 6) == "_input") {
                auto val = parse_double(sysfs_.read_text(base + "/" + f));
                if (val.has_value()) {
                    const std::string prefix = f.substr(0, f.size() - 6);
                    auto label = sysfs_.read_text(base + "/" + prefix + "_label").value_or(prefix);
                    readings.push_back({
                        "hwmon." + dir + "." + prefix,
                        name + " " + label,
                        base + "/" + f,
                        *val / 1000.0,
                        "",
                        true,
                        core::SensorUnit::Celsius,
                        core::CapabilityState::Readable,
                        ts,
                        ""
                    });
                }
            } else if (f.rfind("fan", 0) == 0 && f.size() > 6 && f.substr(f.size() - 6) == "_input") {
                auto val = parse_double(sysfs_.read_text(base + "/" + f));
                if (val.has_value()) {
                    const std::string prefix = f.substr(0, f.size() - 6);
                    auto label = sysfs_.read_text(base + "/" + prefix + "_label").value_or(prefix);
                    readings.push_back({
                        "hwmon." + dir + "." + prefix,
                        name + " " + label,
                        base + "/" + f,
                        *val,
                        "",
                        true,
                        core::SensorUnit::Rpm,
                        core::CapabilityState::Readable,
                        ts,
                        ""
                    });
                }
            }
        }
    }

    // 3. Battery (BAT0)
    const std::string bat_base = "/sys/class/power_supply/BAT0";
    if (sysfs_.exists(bat_base)) {
        auto cap = parse_double(sysfs_.read_text(bat_base + "/capacity"));
        auto status = sysfs_.read_text(bat_base + "/status");
        capabilities.push_back({
            "battery_telemetry",
            "Battery Telemetry (BAT0)",
            backend_name(),
            cap.has_value() ? core::CapabilityState::Readable : core::CapabilityState::Detected,
            bat_base,
            status.has_value() ? ("status: " + *status) : ""
        });
        if (cap.has_value()) {
            readings.push_back({
                "battery.capacity",
                "Battery Charge",
                bat_base + "/capacity",
                *cap,
                "",
                true,
                core::SensorUnit::Percent,
                core::CapabilityState::Readable,
                ts,
                ""
            });
        }
        if (status.has_value()) {
            readings.push_back({
                "battery.status",
                "Battery Status",
                bat_base + "/status",
                0.0,
                *status,
                false,
                core::SensorUnit::Text,
                core::CapabilityState::Readable,
                ts,
                ""
            });
        }
    } else {
        capabilities.push_back({
            "battery_telemetry",
            "Battery Telemetry (BAT0)",
            backend_name(),
            core::CapabilityState::Unsupported,
            bat_base,
            "BAT0 not present"
        });
    }

    // 4. Memory (/proc/meminfo)
    if (auto meminfo = sysfs_.read_text("/proc/meminfo"); meminfo.has_value()) {
        std::istringstream iss(*meminfo);
        std::string line;
        double total_kb = 0.0;
        double avail_kb = 0.0;
        bool has_total = false;
        bool has_avail = false;
        while (std::getline(iss, line)) {
            if (line.rfind("MemTotal:", 0) == 0) {
                std::istringstream ls(line.substr(9));
                if (ls >> total_kb) has_total = true;
            } else if (line.rfind("MemAvailable:", 0) == 0) {
                std::istringstream ls(line.substr(13));
                if (ls >> avail_kb) has_avail = true;
            }
        }
        if (has_total) {
            readings.push_back({
                "memory.total",
                "Memory Total",
                "/proc/meminfo",
                total_kb / 1024.0,
                "",
                true,
                core::SensorUnit::Megabytes,
                core::CapabilityState::Readable,
                ts,
                ""
            });
        }
        if (has_avail) {
            readings.push_back({
                "memory.available",
                "Memory Available",
                "/proc/meminfo",
                avail_kb / 1024.0,
                "",
                true,
                core::SensorUnit::Megabytes,
                core::CapabilityState::Readable,
                ts,
                ""
            });
        }
    }

    // 5. CPU Frequency (cpu0 scaling_cur_freq in kHz -> MHz)
    if (auto freq_khz = parse_double(sysfs_.read_text("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq"));
        freq_khz.has_value()) {
        readings.push_back({
            "cpu.freq_mhz",
            "CPU Frequency",
            "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq",
            *freq_khz / 1000.0,
            "",
            true,
            core::SensorUnit::Megahertz,
            core::CapabilityState::Readable,
            ts,
            ""
        });
    }
    if (auto max_khz = parse_double(sysfs_.read_text("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq"));
        max_khz.has_value()) {
        readings.push_back({
            "cpu.freq_max_mhz",
            "CPU Maximum Frequency",
            "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq",
            *max_khz / 1000.0,
            "",
            true,
            core::SensorUnit::Megahertz,
            core::CapabilityState::Readable,
            ts,
            ""
        });
    }

    // 6. CPU utilization from /proc/stat deltas
    if (auto stat_text = sysfs_.read_text("/proc/stat"); stat_text.has_value()) {
        std::istringstream iss(*stat_text);
        std::string line;
        if (std::getline(iss, line) && line.rfind("cpu ", 0) == 0) {
            std::istringstream values(line.substr(4));
            unsigned long long fields[8] = {0};
            int count = 0;
            while (count < 8 && (values >> fields[count])) {
                ++count;
            }
            if (count >= 4) {
                CpuSample sample;
                for (int i = 0; i < count; ++i) {
                    sample.total += fields[i];
                }
                sample.idle = fields[3] + (count > 4 ? fields[4] : 0ULL);
                if (has_previous_cpu_ && sample.total > previous_cpu_.total) {
                    const double total_delta = static_cast<double>(sample.total - previous_cpu_.total);
                    const double idle_delta = static_cast<double>(sample.idle - previous_cpu_.idle);
                    const double usage = std::clamp(100.0 * (1.0 - idle_delta / total_delta), 0.0, 100.0);
                    readings.push_back({
                        "cpu.usage_percent",
                        "CPU Utilization",
                        "/proc/stat",
                        usage,
                        "",
                        true,
                        core::SensorUnit::Percent,
                        core::CapabilityState::Readable,
                        ts,
                        ""
                    });
                }
                previous_cpu_ = sample;
                has_previous_cpu_ = true;
            }
        }
    }

    // 7. CPU package power from Intel RAPL (microjoules over time)
    const char* rapl_paths[] = {
        "/sys/class/powercap/intel-rapl/intel-rapl:0/energy_uj",
        "/sys/class/powercap/intel-rapl:0/energy_uj"
    };
    for (const char* path : rapl_paths) {
        if (auto energy = parse_double(sysfs_.read_text(path)); energy.has_value()) {
            const double now = steady_seconds();
            RaplSample sample;
            sample.energy_uj = *energy;
            sample.seconds = now;
            sample.valid = true;
            if (previous_rapl_.valid) {
                const double dt = now - previous_rapl_.seconds;
                const double delta = *energy - previous_rapl_.energy_uj;
                if (dt > 0.05 && delta >= 0.0) {
                    const double watts = std::clamp((delta / 1e6) / dt, 0.0, 400.0);
                    readings.push_back({
                        "cpu.power_watts",
                        "CPU Package Power",
                        path,
                        watts,
                        "",
                        true,
                        core::SensorUnit::Watts,
                        core::CapabilityState::Readable,
                        ts,
                        ""
                    });
                }
            }
            previous_rapl_ = sample;
            break;
        }
    }

    // 8. Root filesystem usage
    struct statvfs vfs {};
    if (::statvfs("/", &vfs) == 0 && vfs.f_frsize > 0 && vfs.f_blocks > 0) {
        const double total = static_cast<double>(vfs.f_blocks) * static_cast<double>(vfs.f_frsize);
        const double free = static_cast<double>(vfs.f_bavail) * static_cast<double>(vfs.f_frsize);
        const double used = std::max(0.0, total - free);
        capabilities.push_back({
            "storage_telemetry",
            "Storage Usage",
            backend_name(),
            core::CapabilityState::Readable,
            "/",
            "root filesystem usage"
        });
        readings.push_back({
            "storage.root_total_gb",
            "Storage Total",
            "/",
            total / (1024.0 * 1024.0 * 1024.0),
            "",
            true,
            core::SensorUnit::Gigabytes,
            core::CapabilityState::Readable,
            ts,
            ""
        });
        readings.push_back({
            "storage.root_used_gb",
            "Storage Used",
            "/",
            used / (1024.0 * 1024.0 * 1024.0),
            "",
            true,
            core::SensorUnit::Gigabytes,
            core::CapabilityState::Readable,
            ts,
            ""
        });
        readings.push_back({
            "storage.root_used_percent",
            "Storage Used",
            "/",
            total > 0.0 ? (used / total) * 100.0 : 0.0,
            "",
            true,
            core::SensorUnit::Percent,
            core::CapabilityState::Readable,
            ts,
            ""
        });
    }
}

}  // namespace legion::backends::linux_sysfs
