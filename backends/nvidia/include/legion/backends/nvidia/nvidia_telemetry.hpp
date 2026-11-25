#pragma once

#include "legion/core/interfaces.hpp"

namespace legion::backends::nvidia {

class NvidiaTelemetryProvider final : public core::ITelemetryProvider {
public:
    explicit NvidiaTelemetryProvider(const core::IProcessRunner& runner);
    std::string backend_name() const override { return "nvidia_smi"; }
    void collect(std::vector<core::CapabilityInfo>& capabilities,
                 std::vector<core::SensorReading>& readings) const override;

private:
    const core::IProcessRunner& runner_;
};

}  // namespace legion::backends::nvidia
