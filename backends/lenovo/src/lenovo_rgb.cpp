#include "legion/backends/lenovo/lenovo_rgb.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

namespace legion::backends::lenovo {

namespace {

const char* const kKnownProducts[] = {
    "0000C955", "0000C963", "0000C965", "0000C973", "0000C975",
    "0000C983", "0000C984", "0000C985", "0000C993", "0000C994", "0000C995"
};

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

bool is_known_product(const std::string& product) {
    const std::string value = upper(product);
    for (const char* known : kKnownProducts) {
        if (value == known) return true;
    }
    return false;
}

bool read_uevent(const std::filesystem::path& path, RgbHidDevice& device) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    std::string line;
    std::string hid_id;
    std::string hid_name;
    while (std::getline(file, line)) {
        if (line.rfind("HID_ID=", 0) == 0) {
            hid_id = line.substr(7);
        } else if (line.rfind("HID_NAME=", 0) == 0) {
            hid_name = line.substr(9);
        }
    }
    if (hid_id.empty()) return false;
    const auto first = hid_id.find(':');
    if (first == std::string::npos) return false;
    const auto second = hid_id.find(':', first + 1);
    if (second == std::string::npos) return false;
    device.vendor_id = hid_id.substr(first + 1, second - first - 1);
    device.product_id = hid_id.substr(second + 1);
    device.name = hid_name;
    return upper(device.vendor_id) == "0000048D" && is_known_product(device.product_id);
}

}  // namespace

std::vector<RgbHidDevice> discover_rgb_hid_devices() {
    std::vector<RgbHidDevice> devices;
    std::error_code ec;
    const std::filesystem::path base("/sys/class/hidraw");
    if (!std::filesystem::exists(base, ec)) return devices;
    for (const auto& entry : std::filesystem::directory_iterator(base, ec)) {
        if (ec) break;
        RgbHidDevice device;
        device.path = "/dev/" + entry.path().filename().string();
        if (!std::filesystem::exists(device.path, ec)) continue;
        if (!read_uevent(entry.path() / "device" / "uevent", device)) continue;
        devices.push_back(std::move(device));
    }
    std::sort(devices.begin(), devices.end(), [](const RgbHidDevice& a, const RgbHidDevice& b) {
        return a.path < b.path;
    });
    return devices;
}

RgbReport build_rgb_report(const core::LightingProfile& profile) {
    RgbReport result;
    if (profile.speed < 1 || profile.speed > 4) {
        result.error = "speed must be 1..4";
        return result;
    }
    if (profile.brightness < 1 || profile.brightness > 2) {
        result.error = "brightness must be 1..2";
        return result;
    }
    if (!core::is_hardware_effect(profile.effect)) {
        result.error = "effect is synthesized in software and cannot be sent as a native report";
        return result;
    }

    auto& report = result.bytes;
    report[0] = 0xCC;
    report[1] = 0x16;
    report[3] = static_cast<std::uint8_t>(profile.speed);
    report[4] = static_cast<std::uint8_t>(profile.brightness);

    switch (profile.effect) {
        case core::LightingEffect::Off:
            report[2] = 0x00;
            break;
        case core::LightingEffect::Static:
            report[2] = 0x01;
            break;
        case core::LightingEffect::Breathing:
            report[2] = 0x03;
            break;
        case core::LightingEffect::Wave:
            report[2] = 0x04;
            if (profile.direction == core::WaveDirection::LeftToRight) {
                report[18] = 0x01;
            } else {
                report[19] = 0x01;
            }
            break;
        case core::LightingEffect::Smooth:
            report[2] = 0x06;
            break;
        default:
            result.error = "unsupported hardware effect";
            return result;
    }

    if (profile.effect == core::LightingEffect::Static ||
        profile.effect == core::LightingEffect::Breathing) {
        for (std::size_t zone = 0; zone < profile.zones.size(); ++zone) {
            const std::size_t offset = 5 + zone * 3;
            report[offset + 0] = profile.zones[zone].r;
            report[offset + 1] = profile.zones[zone].g;
            report[offset + 2] = profile.zones[zone].b;
        }
    }

    result.valid = true;
    return result;
}

RgbReport build_static_rgb_report(const std::array<core::Rgb, 4>& zones, int brightness) {
    core::LightingProfile profile;
    profile.effect = core::LightingEffect::Static;
    profile.speed = 1;
    profile.brightness = std::clamp(brightness, 1, 2);
    profile.zones = zones;
    return build_rgb_report(profile);
}

struct RgbSession::Impl {
    std::vector<int> fds;
    std::vector<std::string> paths;

    ~Impl() {
        for (int fd : fds) {
            if (fd >= 0) ::close(fd);
        }
    }

    void close_all() {
        for (int fd : fds) {
            if (fd >= 0) ::close(fd);
        }
        fds.clear();
        paths.clear();
    }
};

RgbSession::RgbSession() : impl_(std::make_unique<Impl>()) {}

RgbSession::~RgbSession() = default;

bool RgbSession::open(std::string& error) {
    if (!impl_->fds.empty()) return true;
    impl_->close_all();
    const auto devices = discover_rgb_hid_devices();
    if (devices.empty()) {
        error = "no supported ITE 4-zone RGB hidraw device found";
        return false;
    }
    for (const auto& device : devices) {
        const int fd = ::open(device.path.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        impl_->fds.push_back(fd);
        impl_->paths.push_back(device.path);
    }
    if (impl_->fds.empty()) {
        error = "RGB device found but could not be opened (check udev uaccess rule)";
        return false;
    }
    return true;
}

bool RgbSession::is_open() const {
    return !impl_->fds.empty();
}

bool RgbSession::send(const RgbReport& report, std::string& error) {
    if (!report.valid) {
        error = report.error.empty() ? "invalid report" : report.error;
        return false;
    }
    if (impl_->fds.empty()) {
        error = "RGB session is not open";
        return false;
    }
    int successes = 0;
    int last_errno = 0;
    for (int fd : impl_->fds) {
        const int ret = ::ioctl(fd, HIDIOCSFEATURE(report.bytes.size()), report.bytes.data());
        if (ret >= 0) {
            ++successes;
        } else {
            last_errno = errno;
        }
    }
    if (successes == 0) {
        error = std::string("HIDIOCSFEATURE failed: ") + std::strerror(last_errno);
        return false;
    }
    return true;
}

bool RgbSession::apply(const core::LightingProfile& profile, std::string& error) {
    if (core::is_software_effect(profile.effect)) {
        const auto frame = core::sample_lighting_frame(profile.effect, profile, 0.0);
        return apply_zones(frame, profile.brightness, error);
    }
    const auto report = build_rgb_report(profile);
    return send(report, error);
}

bool RgbSession::apply_zones(const std::array<core::Rgb, 4>& zones, int brightness, std::string& error) {
    const auto report = build_static_rgb_report(zones, brightness);
    return send(report, error);
}

std::string RgbSession::device_summary() const {
    std::string summary;
    for (std::size_t i = 0; i < impl_->paths.size(); ++i) {
        if (i != 0) summary += ", ";
        summary += impl_->paths[i];
    }
    return summary;
}

}  // namespace legion::backends::lenovo
