#include "legion/backends/nvidia/nvidia_telemetry.hpp"

#include <sstream>
#include <vector>

namespace legion::backends::nvidia {

namespace {

std::string trim(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> parts;
    std::istringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) {
        parts.push_back(trim(item));
    }
    return parts;
}

}  // namespace

NvidiaTelemetryProvider::NvidiaTelemetryProvider(const core::IProcessRunner& runner)
    : runner_(runner) {}

void NvidiaTelemetryProvider::collect(std::vector<core::CapabilityInfo>& capabilities,
                                      std::vector<core::SensorReading>& readings) const {
    const std::string ts = core::current_timestamp_iso8601();
    const std::vector<std::string> args = {
        "nvidia-smi",
        "--query-gpu=name,driver_version,temperature.gpu,power.draw,utilization.gpu,memory.used,memory.total,clocks.current.graphics,fan.speed,power.max_limit",
        "--format=csv,noheader,nounits"
    };

    const auto res = runner_.run(args, 2000);
    if (res.spawn_failed || res.timed_out || res.exit_code != 0) {
        capabilities.push_back({
            "nvidia_gpu_telemetry",
            "NVIDIA GPU Telemetry",
            backend_name(),
            res.timed_out ? core::CapabilityState::TemporarilyUnavailable
                          : core::CapabilityState::Unsupported,
            "nvidia-smi",
            res.timed_out ? "nvidia-smi timed out" : "nvidia-smi unavailable or failed"
        });
        return;
    }

    std::istringstream iss(res.stdout_text);
    std::string line;
    if (!std::getline(iss, line) || trim(line).empty()) {
        capabilities.push_back({
            "nvidia_gpu_telemetry",
            "NVIDIA GPU Telemetry",
            backend_name(),
            core::CapabilityState::Failed,
            "nvidia-smi",
            "empty output from nvidia-smi"
        });
        return;
    }

    auto cols = split_csv_line(line);
    if (cols.size() < 10) {
        capabilities.push_back({
            "nvidia_gpu_telemetry",
            "NVIDIA GPU Telemetry",
            backend_name(),
            core::CapabilityState::Failed,
            "nvidia-smi",
            "unexpected column count"
        });
        return;
    }

    capabilities.push_back({
        "nvidia_gpu_telemetry",
        "NVIDIA GPU Telemetry",
        backend_name(),
        core::CapabilityState::Readable,
        "nvidia-smi",
        cols[0] + " (driver " + cols[1] + ")"
    });

    auto add_numeric = [&](const std::string& id, const std::string& label,
                           const std::string& raw, core::SensorUnit unit) {
        try {
            double v = std::stod(raw);
            readings.push_back({id, label, "nvidia-smi", v, "", true, unit,
                                core::CapabilityState::Readable, ts, ""});
        } catch (...) {
            readings.push_back({id, label, "nvidia-smi", 0.0, raw, false, core::SensorUnit::Text,
                                core::CapabilityState::TemporarilyUnavailable, ts,
                                "non-numeric value: " + raw});
        }
    };

    add_numeric("nvidia.gpu.temp", "NVIDIA GPU Temperature", cols[2], core::SensorUnit::Celsius);
    add_numeric("nvidia.gpu.power", "NVIDIA GPU Power Draw", cols[3], core::SensorUnit::Watts);
    add_numeric("nvidia.gpu.util", "NVIDIA GPU Utilization", cols[4], core::SensorUnit::Percent);
    add_numeric("nvidia.gpu.vram_used", "NVIDIA VRAM Used", cols[5], core::SensorUnit::Megabytes);
    add_numeric("nvidia.gpu.vram_total", "NVIDIA VRAM Total", cols[6], core::SensorUnit::Megabytes);
    add_numeric("nvidia.gpu.clock_mhz", "NVIDIA GPU Clock", cols[7], core::SensorUnit::Megahertz);
    add_numeric("nvidia.gpu.fan_percent", "NVIDIA GPU Fan", cols[8], core::SensorUnit::Percent);
    add_numeric("nvidia.gpu.power_limit", "NVIDIA GPU Max Power Limit", cols[9], core::SensorUnit::Watts);
}

}  // namespace legion::backends::nvidia
