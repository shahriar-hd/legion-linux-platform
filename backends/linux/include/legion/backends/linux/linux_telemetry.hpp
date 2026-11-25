#pragma once

#include "legion/core/interfaces.hpp"

namespace legion::backends::linux_sysfs {

class LinuxTelemetryProvider final : public core::ITelemetryProvider {
public:
    explicit LinuxTelemetryProvider(const core::ISysfsReader& sysfs);
    std::string backend_name() const override { return "linux_sysfs"; }
    void collect(std::vector<core::CapabilityInfo>& capabilities,
                 std::vector<core::SensorReading>& readings) const override;

    core::SystemSnapshot collect_system_metadata() const;

private:
    struct CpuSample {
        unsigned long long total = 0;
        unsigned long long idle = 0;
    };

    struct RaplSample {
        double energy_uj = 0.0;
        double seconds = 0.0;
        bool valid = false;
    };

    const core::ISysfsReader& sysfs_;
    mutable CpuSample previous_cpu_;
    mutable bool has_previous_cpu_ = false;
    mutable RaplSample previous_rapl_;
};

}  // namespace legion::backends::linux_sysfs
