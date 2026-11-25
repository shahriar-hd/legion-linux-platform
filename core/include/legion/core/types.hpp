#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace legion::core {

enum class CapabilityState {
    Unsupported,
    Detected,
    Readable,
    Writable,
    Applied,
    Verified,
    TemporarilyUnavailable,
    Failed
};

inline std::string to_string(CapabilityState state) {
    switch (state) {
        case CapabilityState::Unsupported: return "unsupported";
        case CapabilityState::Detected: return "detected";
        case CapabilityState::Readable: return "readable";
        case CapabilityState::Writable: return "writable";
        case CapabilityState::Applied: return "applied";
        case CapabilityState::Verified: return "verified";
        case CapabilityState::TemporarilyUnavailable: return "temporarily_unavailable";
        case CapabilityState::Failed: return "failed";
    }
    return "unknown";
}

enum class SensorUnit {
    Celsius,
    Milliwatts,
    Watts,
    Percent,
    Megabytes,
    Gigabytes,
    Megahertz,
    Rpm,
    Volts,
    Millivolts,
    Text,
    None
};

inline std::string to_string(SensorUnit unit) {
    switch (unit) {
        case SensorUnit::Celsius: return "C";
        case SensorUnit::Milliwatts: return "mW";
        case SensorUnit::Watts: return "W";
        case SensorUnit::Percent: return "%";
        case SensorUnit::Megabytes: return "MB";
        case SensorUnit::Gigabytes: return "GB";
        case SensorUnit::Megahertz: return "MHz";
        case SensorUnit::Rpm: return "RPM";
        case SensorUnit::Volts: return "V";
        case SensorUnit::Millivolts: return "mV";
        case SensorUnit::Text: return "text";
        case SensorUnit::None: return "";
    }
    return "";
}

struct SensorReading {
    std::string id;
    std::string label;
    std::string source;
    double numeric_value = 0.0;
    std::string text_value;
    bool is_numeric = true;
    SensorUnit unit = SensorUnit::None;
    CapabilityState state = CapabilityState::Unsupported;
    std::string timestamp_iso8601;
    std::string error_message;
};

struct CapabilityInfo {
    std::string id;
    std::string name;
    std::string backend;
    CapabilityState state = CapabilityState::Unsupported;
    std::string evidence;
    std::string notes;
};

struct SystemSnapshot {
    std::string product_name;
    std::string product_version;
    std::string bios_version;
    std::string kernel_version;
    std::string timestamp_iso8601;
    std::vector<CapabilityInfo> capabilities;
    std::vector<SensorReading> readings;
};

std::string current_timestamp_iso8601();
std::string to_json(const SystemSnapshot& snapshot);

}  // namespace legion::core
