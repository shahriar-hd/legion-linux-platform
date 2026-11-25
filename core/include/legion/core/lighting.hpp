#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace legion::core {

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

enum class LightingEffect {
    Off,
    Static,
    Breathing,
    Wave,
    Smooth,
    Heartbeat,
    Rainbow,
    Dance,
    Random,
    Reactive
};

enum class WaveDirection {
    LeftToRight,
    RightToLeft
};

struct LightingProfile {
    std::string name = "Profile";
    LightingEffect effect = LightingEffect::Static;
    int speed = 2;
    int brightness = 2;
    WaveDirection direction = WaveDirection::LeftToRight;
    std::array<Rgb, 4> zones{};
};

struct LightingConfig {
    std::array<LightingProfile, 4> profiles{};
    int active_profile = 0;
    bool apply_on_login = true;
    LightingConfig();
};

std::string to_string(LightingEffect effect);
std::string to_string(WaveDirection direction);
bool parse_lighting_effect(const std::string& text, LightingEffect& out);
bool parse_wave_direction(const std::string& text, WaveDirection& out);

bool is_hardware_effect(LightingEffect effect);
bool is_software_effect(LightingEffect effect);
bool effect_uses_zones(LightingEffect effect);
bool effect_uses_direction(LightingEffect effect);

std::string serialize_lighting_config(const LightingConfig& config);
std::optional<LightingConfig> parse_lighting_config(const std::string& text);

Rgb hsv_to_rgb(double hue, double saturation, double value);
std::array<Rgb, 4> sample_lighting_frame(LightingEffect effect,
                                         const LightingProfile& profile,
                                         double time_seconds);

std::string default_lighting_config_path();
std::string default_lightd_socket_path();
std::string default_config_dir();

std::optional<std::string> read_text_file(const std::string& path);
bool write_text_file_atomic(const std::string& path, const std::string& content, std::string& error);
std::int64_t file_mtime_ns(const std::string& path);

}  // namespace legion::core
