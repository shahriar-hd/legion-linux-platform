#include "legion/backends/lenovo/lenovo_telemetry.hpp"

#include <optional>
#include <string>

namespace legion::backends::lenovo {

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

}  // namespace

LenovoTelemetryProvider::LenovoTelemetryProvider(const core::ISysfsReader& sysfs)
    : sysfs_(sysfs) {}

void LenovoTelemetryProvider::collect(std::vector<core::CapabilityInfo>& capabilities,
                                      std::vector<core::SensorReading>& readings) const {
    const std::string ts = core::current_timestamp_iso8601();
    const std::string vpc_base = "/sys/bus/platform/drivers/ideapad_acpi/VPC2004:00";

    if (sysfs_.exists(vpc_base)) {
        capabilities.push_back({
            "lenovo_vpc2004",
            "Lenovo ACPI Controls (VPC2004:00)",
            backend_name(),
            core::CapabilityState::Readable,
            vpc_base,
            "conservation_mode, fan_mode, fn_lock, usb_charging, camera_power"
        });

        auto read_flag = [&](const std::string& attr, const std::string& id, const std::string& label) {
            const std::string path = vpc_base + "/" + attr;
            if (auto val = sysfs_.read_text(path); val.has_value()) {
                readings.push_back({
                    id,
                    label,
                    path,
                    0.0,
                    *val,
                    false,
                    core::SensorUnit::Text,
                    core::CapabilityState::Readable,
                    ts,
                    ""
                });
            }
        };

        read_flag("conservation_mode", "lenovo.conservation_mode", "Conservation Mode");
        read_flag("fan_mode", "lenovo.fan_mode", "Fan Mode");
        read_flag("fn_lock", "lenovo.fn_lock", "Fn Lock");
        read_flag("usb_charging", "lenovo.usb_charging", "Always-On USB Charging");
        read_flag("camera_power", "lenovo.camera_power", "Camera Power");
    } else {
        capabilities.push_back({
            "lenovo_vpc2004",
            "Lenovo ACPI Controls (VPC2004:00)",
            backend_name(),
            core::CapabilityState::Unsupported,
            vpc_base,
            "VPC2004:00 not present"
        });
    }

    // Extended BAT0 health & charging telemetry
    const std::string bat_base = "/sys/class/power_supply/BAT0";
    if (sysfs_.exists(bat_base)) {
        if (auto ct = sysfs_.read_text(bat_base + "/charge_types"); ct.has_value()) {
            readings.push_back({
                "battery.charge_types",
                "Battery Charge Type",
                bat_base + "/charge_types",
                0.0,
                *ct,
                false,
                core::SensorUnit::Text,
                core::CapabilityState::Readable,
                ts,
                ""
            });
        }
        if (auto cc = parse_double(sysfs_.read_text(bat_base + "/cycle_count")); cc.has_value()) {
            readings.push_back({
                "battery.cycle_count",
                "Battery Cycle Count",
                bat_base + "/cycle_count",
                *cc,
                "",
                true,
                core::SensorUnit::None,
                core::CapabilityState::Readable,
                ts,
                ""
            });
        }
        auto full = parse_double(sysfs_.read_text(bat_base + "/energy_full"));
        auto design = parse_double(sysfs_.read_text(bat_base + "/energy_full_design"));
        if (full.has_value() && design.has_value() && *design > 0.0) {
            double health_pct = (*full / *design) * 100.0;
            readings.push_back({
                "battery.health_percent",
                "Battery Health",
                bat_base + "/energy_full",
                health_pct,
                "",
                true,
                core::SensorUnit::Percent,
                core::CapabilityState::Readable,
                ts,
                ""
            });
        }
        if (auto pwr = parse_double(sysfs_.read_text(bat_base + "/power_now")); pwr.has_value()) {
            readings.push_back({
                "battery.power_watts",
                "Battery Power",
                bat_base + "/power_now",
                *pwr / 1000000.0,
                "",
                true,
                core::SensorUnit::Watts,
                core::CapabilityState::Readable,
                ts,
                ""
            });
        }
    }

    // 4-zone RGB keyboard detection (048D:C965)
    auto hidraw_dirs = sysfs_.list_dir("/sys/class/hidraw");
    bool rgb_detected = false;
    std::string rgb_evidence;
    for (const auto& h : hidraw_dirs) {
        const std::string uevent_path = "/sys/class/hidraw/" + h + "/device/uevent";
        if (auto uevent = sysfs_.read_text(uevent_path); uevent.has_value()) {
            if (uevent->find("0000048D:0000C965") != std::string::npos) {
                rgb_detected = true;
                rgb_evidence = "/dev/" + h + " (ITE 048D:C965)";
                break;
            }
        }
    }
    capabilities.push_back({
        "lenovo_4zone_rgb",
        "4-Zone RGB Keyboard (ITE 8295)",
        backend_name(),
        rgb_detected ? core::CapabilityState::Detected : core::CapabilityState::Unsupported,
        rgb_detected ? rgb_evidence : "/sys/class/hidraw",
        rgb_detected ? "HID device detected; writes use hidraw (udev uaccess) or legion-helper; confirm physical result" : "048D:C965 not found"
    });
}

}  // namespace legion::backends::lenovo
