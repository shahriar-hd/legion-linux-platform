#include "legion/backends/lenovo/lenovo_telemetry.hpp"
#include "legion/backends/linux/linux_telemetry.hpp"
#include "legion/backends/nvidia/nvidia_telemetry.hpp"
#include "legion/core/interfaces.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

class MockSysfsReader final : public legion::core::ISysfsReader {
public:
    std::map<std::string, std::string> files;
    std::map<std::string, std::vector<std::string>> dirs;

    bool exists(const std::string& path) const override {
        return files.count(path) > 0 || dirs.count(path) > 0;
    }

    std::optional<std::string> read_text(const std::string& path) const override {
        auto it = files.find(path);
        if (it == files.end()) return std::nullopt;
        return it->second;
    }

    std::vector<std::string> list_dir(const std::string& path) const override {
        auto it = dirs.find(path);
        if (it == dirs.end()) return {};
        return it->second;
    }
};

class MockProcessRunner final : public legion::core::IProcessRunner {
public:
    legion::core::ProcessResult next_result;

    legion::core::ProcessResult run(const std::vector<std::string>& /*args*/,
                                    int /*timeout_ms*/) const override {
        return next_result;
    }
};

void test_linux_telemetry_reads_mock_sensors() {
    MockSysfsReader sysfs;
    sysfs.files["/sys/class/dmi/id/product_name"] = "82JF";
    sysfs.files["/sys/class/dmi/id/product_version"] = "Legion 5 Pro 16ITH6";
    sysfs.files["/sys/class/dmi/id/bios_version"] = "H1CN58WW";
    sysfs.files["/proc/sys/kernel/osrelease"] = "7.0.0-34-generic";
    sysfs.files["/sys/firmware/acpi/platform_profile"] = "balanced";
    sysfs.files["/sys/firmware/acpi/platform_profile_choices"] = "low-power balanced performance custom";
    sysfs.dirs["/sys/class/hwmon"] = {"hwmon4"};
    sysfs.dirs["/sys/class/hwmon/hwmon4"] = {"name", "temp1_input", "temp1_label"};
    sysfs.files["/sys/class/hwmon/hwmon4/name"] = "coretemp";
    sysfs.files["/sys/class/hwmon/hwmon4/temp1_input"] = "45000";
    sysfs.files["/sys/class/hwmon/hwmon4/temp1_label"] = "Package id 0";
    sysfs.dirs["/sys/class/power_supply/BAT0"] = {"capacity", "status"};
    sysfs.files["/sys/class/power_supply/BAT0/capacity"] = "82";
    sysfs.files["/sys/class/power_supply/BAT0/status"] = "Charging";
    sysfs.files["/proc/meminfo"] = "MemTotal:       16384000 kB\nMemAvailable:    8192000 kB\n";

    legion::backends::linux_sysfs::LinuxTelemetryProvider provider(sysfs);
    auto snap = provider.collect_system_metadata();
    assert(snap.product_name == "82JF");
    assert(snap.bios_version == "H1CN58WW");

    provider.collect(snap.capabilities, snap.readings);

    bool found_temp = false;
    bool found_bat = false;
    for (const auto& r : snap.readings) {
        if (r.id == "hwmon.hwmon4.temp1") {
            found_temp = true;
            assert(std::abs(r.numeric_value - 45.0) < 0.01);
            assert(r.unit == legion::core::SensorUnit::Celsius);
        } else if (r.id == "battery.capacity") {
            found_bat = true;
            assert(std::abs(r.numeric_value - 82.0) < 0.01);
            assert(r.unit == legion::core::SensorUnit::Percent);
        }
    }
    assert(found_temp);
    assert(found_bat);
}

void test_nvidia_telemetry_parses_csv_and_handles_timeout() {
    MockProcessRunner runner;
    runner.next_result = {0, "NVIDIA GeForce RTX 3050 Ti Laptop GPU, 595.104.02, 48, 12.50, 5, 512, 4096, 1450, 37, 80.00\n", "", false, false};

    legion::backends::nvidia::NvidiaTelemetryProvider provider(runner);
    std::vector<legion::core::CapabilityInfo> caps;
    std::vector<legion::core::SensorReading> readings;
    provider.collect(caps, readings);

    assert(caps.size() == 1);
    assert(caps[0].state == legion::core::CapabilityState::Readable);
    assert(readings.size() == 8);
    assert(std::abs(readings[0].numeric_value - 48.0) < 0.01);
    assert(std::abs(readings[1].numeric_value - 12.5) < 0.01);
    assert(std::abs(readings[5].numeric_value - 1450.0) < 0.01);
    assert(std::abs(readings[6].numeric_value - 37.0) < 0.01);
    assert(std::abs(readings[7].numeric_value - 80.0) < 0.01);

    // Timeout case
    runner.next_result = {-1, "", "", true, false};
    caps.clear();
    readings.clear();
    provider.collect(caps, readings);
    assert(caps.size() == 1);
    assert(caps[0].state == legion::core::CapabilityState::TemporarilyUnavailable);
    assert(readings.empty());
}

void test_lenovo_telemetry_reads_vpc_and_rgb() {
    MockSysfsReader sysfs;
    sysfs.dirs["/sys/bus/platform/drivers/ideapad_acpi/VPC2004:00"] = {"conservation_mode", "fan_mode"};
    sysfs.files["/sys/bus/platform/drivers/ideapad_acpi/VPC2004:00/conservation_mode"] = "1";
    sysfs.files["/sys/bus/platform/drivers/ideapad_acpi/VPC2004:00/fan_mode"] = "0";
    sysfs.dirs["/sys/class/power_supply/BAT0"] = {"charge_types", "cycle_count", "energy_full", "energy_full_design"};
    sysfs.files["/sys/class/power_supply/BAT0/charge_types"] = "Fast Standard [Long_Life]";
    sysfs.files["/sys/class/power_supply/BAT0/cycle_count"] = "42";
    sysfs.files["/sys/class/power_supply/BAT0/energy_full"] = "72000000";
    sysfs.files["/sys/class/power_supply/BAT0/energy_full_design"] = "80000000";
    sysfs.dirs["/sys/class/hidraw"] = {"hidraw1"};
    sysfs.files["/sys/class/hidraw/hidraw1/device/uevent"] = "HID_ID=0003:0000048D:0000C965\n";

    legion::backends::lenovo::LenovoTelemetryProvider provider(sysfs);
    std::vector<legion::core::CapabilityInfo> caps;
    std::vector<legion::core::SensorReading> readings;
    provider.collect(caps, readings);

    assert(caps.size() == 2);
    assert(caps[0].id == "lenovo_vpc2004" && caps[0].state == legion::core::CapabilityState::Readable);
    assert(caps[1].id == "lenovo_4zone_rgb" && caps[1].state == legion::core::CapabilityState::Detected);

    bool found_cons = false;
    bool found_health = false;
    for (const auto& r : readings) {
        if (r.id == "lenovo.conservation_mode") {
            found_cons = true;
            assert(r.text_value == "1");
        } else if (r.id == "battery.health_percent") {
            found_health = true;
            assert(std::abs(r.numeric_value - 90.0) < 0.01);
        }
    }
    assert(found_cons);
    assert(found_health);
}

}  // namespace

int main() {
    test_linux_telemetry_reads_mock_sensors();
    test_nvidia_telemetry_parses_csv_and_handles_timeout();
    test_lenovo_telemetry_reads_vpc_and_rgb();
    std::cout << "All unit tests passed.\n";
    return 0;
}
