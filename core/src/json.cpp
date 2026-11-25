#include "legion/core/types.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace legion::core {

namespace {

std::string escape_json(const std::string& s) {
    std::ostringstream o;
    for (unsigned char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (c < 0x20) {
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    o << static_cast<char>(c);
                }
        }
    }
    return o.str();
}

}  // namespace

std::string current_timestamp_iso8601() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_utc{};
    gmtime_r(&t, &tm_utc);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    return std::string(buf);
}

std::string to_json(const SystemSnapshot& snapshot) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"product_name\": \"" << escape_json(snapshot.product_name) << "\",\n";
    o << "  \"product_version\": \"" << escape_json(snapshot.product_version) << "\",\n";
    o << "  \"bios_version\": \"" << escape_json(snapshot.bios_version) << "\",\n";
    o << "  \"kernel_version\": \"" << escape_json(snapshot.kernel_version) << "\",\n";
    o << "  \"timestamp_iso8601\": \"" << escape_json(snapshot.timestamp_iso8601) << "\",\n";

    o << "  \"capabilities\": [\n";
    for (size_t i = 0; i < snapshot.capabilities.size(); ++i) {
        const auto& c = snapshot.capabilities[i];
        o << "    {\n";
        o << "      \"id\": \"" << escape_json(c.id) << "\",\n";
        o << "      \"name\": \"" << escape_json(c.name) << "\",\n";
        o << "      \"backend\": \"" << escape_json(c.backend) << "\",\n";
        o << "      \"state\": \"" << escape_json(to_string(c.state)) << "\",\n";
        o << "      \"evidence\": \"" << escape_json(c.evidence) << "\",\n";
        o << "      \"notes\": \"" << escape_json(c.notes) << "\"\n";
        o << "    }" << (i + 1 < snapshot.capabilities.size() ? "," : "") << "\n";
    }
    o << "  ],\n";

    o << "  \"readings\": [\n";
    for (size_t i = 0; i < snapshot.readings.size(); ++i) {
        const auto& r = snapshot.readings[i];
        o << "    {\n";
        o << "      \"id\": \"" << escape_json(r.id) << "\",\n";
        o << "      \"label\": \"" << escape_json(r.label) << "\",\n";
        o << "      \"source\": \"" << escape_json(r.source) << "\",\n";
        if (r.is_numeric) {
            o << "      \"value\": " << r.numeric_value << ",\n";
        } else {
            o << "      \"value\": \"" << escape_json(r.text_value) << "\",\n";
        }
        o << "      \"unit\": \"" << escape_json(to_string(r.unit)) << "\",\n";
        o << "      \"state\": \"" << escape_json(to_string(r.state)) << "\",\n";
        o << "      \"timestamp_iso8601\": \"" << escape_json(r.timestamp_iso8601) << "\",\n";
        o << "      \"error_message\": \"" << escape_json(r.error_message) << "\"\n";
        o << "    }" << (i + 1 < snapshot.readings.size() ? "," : "") << "\n";
    }
    o << "  ]\n";
    o << "}\n";
    return o.str();
}

}  // namespace legion::core
