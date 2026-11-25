#pragma once

#include "legion/core/interfaces.hpp"

namespace legion::backends::lenovo {

class LenovoTelemetryProvider final : public core::ITelemetryProvider {
public:
    explicit LenovoTelemetryProvider(const core::ISysfsReader& sysfs);
    std::string backend_name() const override { return "lenovo_acpi"; }
    void collect(std::vector<core::CapabilityInfo>& capabilities,
                 std::vector<core::SensorReading>& readings) const override;

private:
    const core::ISysfsReader& sysfs_;
};

}  // namespace legion::backends::lenovo
