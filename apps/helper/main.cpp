#include "legion/backends/lenovo/lenovo_rgb.hpp"
#include "legion/core/lighting.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

struct ControlTarget {
    std::string sysfs_path;
    std::set<std::string> allowed_values;
};

const std::map<std::string, ControlTarget>& get_allowlist() {
    static const std::map<std::string, ControlTarget> kTargets = {
        {"platform_profile", {
            "/sys/firmware/acpi/platform_profile",
            {"low-power", "balanced", "performance", "custom"}
        }},
        {"conservation_mode", {
            "/sys/bus/platform/drivers/ideapad_acpi/VPC2004:00/conservation_mode",
            {"0", "1"}
        }},
        {"fn_lock", {
            "/sys/bus/platform/drivers/ideapad_acpi/VPC2004:00/fn_lock",
            {"0", "1"}
        }},
        {"usb_charging", {
            "/sys/bus/platform/drivers/ideapad_acpi/VPC2004:00/usb_charging",
            {"0", "1"}
        }},
        {"camera_power", {
            "/sys/bus/platform/drivers/ideapad_acpi/VPC2004:00/camera_power",
            {"0", "1"}
        }},
    };
    return kTargets;
}

int parse_byte(const std::string& text) {
    const int value = std::stoi(text);
    if (value < 0 || value > 255) {
        throw std::out_of_range("value out of range");
    }
    return value;
}

int handle_set(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: legion-helper set <key> <value>\n";
        return 2;
    }
    const std::string key = argv[2];
    const std::string value = argv[3];

    const auto& allowlist = get_allowlist();
    const auto it = allowlist.find(key);
    if (it == allowlist.end()) {
        std::cerr << "Error: unsupported control key: " << key << "\n";
        return 3;
    }
    if (it->second.allowed_values.count(value) == 0) {
        std::cerr << "Error: invalid value '" << value << "' for key '" << key << "'\n";
        return 4;
    }

    std::ofstream out(it->second.sysfs_path);
    if (!out.is_open()) {
        std::cerr << "Error: failed to open " << it->second.sysfs_path << " for writing\n";
        return 5;
    }
    out << value << "\n";
    out.flush();
    if (out.fail()) {
        std::cerr << "Error: write failed for " << it->second.sysfs_path << "\n";
        return 6;
    }
    out.close();

    std::ifstream in(it->second.sysfs_path);
    std::string readback;
    if (in.is_open() && std::getline(in, readback)) {
        std::cout << "Applied " << key << "=" << value << " (readback: " << readback << ")\n";
    } else {
        std::cout << "Applied " << key << "=" << value << "\n";
    }
    return 0;
}

void print_rgb_usage() {
    std::cerr
        << "Usage:\n"
        << "  legion-helper rgb off\n"
        << "  legion-helper rgb <static|breathing|wave|smooth> <speed:1-4> <brightness:1-2> <ltr|rtl|none> "
           "r1 g1 b1 r2 g2 b2 r3 g3 b3 r4 g4 b4\n";
}

int handle_rgb(int argc, char* argv[]) {
    legion::core::LightingProfile profile;
    profile.effect = legion::core::LightingEffect::Static;
    profile.speed = 1;
    profile.brightness = 2;
    profile.direction = legion::core::WaveDirection::LeftToRight;
    profile.zones = {legion::core::Rgb{0, 212, 255}, legion::core::Rgb{0, 212, 255},
                     legion::core::Rgb{0, 212, 255}, legion::core::Rgb{0, 212, 255}};

    if (argc == 3 && std::string(argv[2]) == "off") {
        profile.effect = legion::core::LightingEffect::Off;
        profile.speed = 1;
        profile.brightness = 2;
    } else if (argc == 18) {
        if (!legion::core::parse_lighting_effect(argv[2], profile.effect)) {
            std::cerr << "Error: unsupported RGB effect: " << argv[2] << "\n";
            print_rgb_usage();
            return 3;
        }
        if (!legion::core::is_hardware_effect(profile.effect)) {
            std::cerr << "Error: effect '" << argv[2]
                      << "' is synthesized in software; use legion-lightd or the GUI\n";
            return 3;
        }
        try {
            profile.speed = std::clamp(std::stoi(argv[3]), 1, 4);
            profile.brightness = std::clamp(std::stoi(argv[4]), 1, 2);
            const std::string direction = argv[5];
            if (direction == "rtl" || direction == "left" || direction == "right-to-left") {
                profile.direction = legion::core::WaveDirection::RightToLeft;
            } else {
                profile.direction = legion::core::WaveDirection::LeftToRight;
            }
            for (int zone = 0; zone < 4; ++zone) {
                const int offset = 6 + zone * 3;
                profile.zones[static_cast<std::size_t>(zone)] = {
                    static_cast<std::uint8_t>(parse_byte(argv[offset + 0])),
                    static_cast<std::uint8_t>(parse_byte(argv[offset + 1])),
                    static_cast<std::uint8_t>(parse_byte(argv[offset + 2]))};
            }
        } catch (const std::exception& error) {
            std::cerr << "Error: invalid RGB argument: " << error.what() << "\n";
            return 3;
        }
    } else {
        print_rgb_usage();
        return 2;
    }

    const auto report = legion::backends::lenovo::build_rgb_report(profile);
    if (!report.valid) {
        std::cerr << "Error: " << report.error << "\n";
        return 4;
    }

    legion::backends::lenovo::RgbSession session;
    std::string error;
    if (!session.open(error)) {
        std::cerr << "Error: " << error << "\n";
        return 5;
    }
    if (!session.send(report, error)) {
        std::cerr << "Error: " << error << "\n";
        return 6;
    }

    std::cout << "Applied " << legion::core::to_string(profile.effect)
              << " RGB (write reported on " << session.device_summary()
              << "; confirm the physical keyboard)\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: legion-helper set <key> <value>\n"
                  << "       legion-helper rgb off\n"
                  << "       legion-helper rgb <effect> <speed> <brightness> <ltr|rtl|none> "
                     "r1 g1 b1 r2 g2 b2 r3 g3 b3 r4 g4 b4\n";
        return 2;
    }

    const std::string command = argv[1];
    if (command == "set") {
        return handle_set(argc, argv);
    }
    if (command == "rgb") {
        return handle_rgb(argc, argv);
    }

    std::cerr << "Error: unknown command: " << command << "\n";
    return 2;
}
