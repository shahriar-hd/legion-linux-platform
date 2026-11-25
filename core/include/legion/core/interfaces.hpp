#pragma once

#include "legion/core/types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace legion::core {

/// Read-only abstraction over sysfs/procfs filesystem access for unit testing.
class ISysfsReader {
public:
    virtual ~ISysfsReader() = default;
    virtual bool exists(const std::string& path) const = 0;
    virtual std::optional<std::string> read_text(const std::string& path) const = 0;
    virtual std::vector<std::string> list_dir(const std::string& path) const = 0;
};

struct ProcessResult {
    int exit_code = -1;
    std::string stdout_text;
    std::string stderr_text;
    bool timed_out = false;
    bool spawn_failed = false;
};

/// Safe process runner using argument arrays (no shell interpolation).
class IProcessRunner {
public:
    virtual ~IProcessRunner() = default;
    virtual ProcessResult run(const std::vector<std::string>& args, int timeout_ms) const = 0;
};

/// Common interface for read-only telemetry backends.
class ITelemetryProvider {
public:
    virtual ~ITelemetryProvider() = default;
    virtual std::string backend_name() const = 0;
    virtual void collect(std::vector<CapabilityInfo>& capabilities,
                         std::vector<SensorReading>& readings) const = 0;
};

std::unique_ptr<ISysfsReader> create_posix_sysfs_reader();
std::unique_ptr<IProcessRunner> create_posix_process_runner();

}  // namespace legion::core
