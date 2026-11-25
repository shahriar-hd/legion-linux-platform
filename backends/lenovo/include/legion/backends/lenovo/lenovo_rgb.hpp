#pragma once

#include "legion/core/lighting.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace legion::backends::lenovo {

struct RgbHidDevice {
    std::string path;
    std::string vendor_id;
    std::string product_id;
    std::string name;
};

std::vector<RgbHidDevice> discover_rgb_hid_devices();

struct RgbReport {
    std::array<std::uint8_t, 33> bytes{};
    bool valid = false;
    std::string error;
};

RgbReport build_rgb_report(const core::LightingProfile& profile);
RgbReport build_static_rgb_report(const std::array<core::Rgb, 4>& zones, int brightness);

class RgbSession {
public:
    RgbSession();
    ~RgbSession();
    RgbSession(const RgbSession&) = delete;
    RgbSession& operator=(const RgbSession&) = delete;

    bool open(std::string& error);
    bool is_open() const;
    bool send(const RgbReport& report, std::string& error);
    bool apply(const core::LightingProfile& profile, std::string& error);
    bool apply_zones(const std::array<core::Rgb, 4>& zones, int brightness, std::string& error);
    void reset();
    std::string device_summary() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace legion::backends::lenovo
