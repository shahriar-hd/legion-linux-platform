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

enum class ReactiveMode {
    Preset,
    Random
};

struct LightingProfile {
    std::string name = "Profile";
    LightingEffect effect = LightingEffect::Static;
    int speed = 2;
    int brightness = 2;
    WaveDirection direction = WaveDirection::LeftToRight;
    ReactiveMode reactive_mode = ReactiveMode::Preset;
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
std::string to_string(ReactiveMode mode);
bool parse_lighting_effect(const std::string& text, LightingEffect& out);
bool parse_wave_direction(const std::string& text, WaveDirection& out);
bool parse_reactive_mode(const std::string& text, ReactiveMode& out);

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

// Stateful reactive effect: keys light up the zone they belong to and fade out.
struct ReactiveFlashState {
    std::array<double, 4> intensity{};
    std::array<Rgb, 4> color{};
    double last_time = 0.0;

    bool active() const {
        for (double value : intensity) {
            if (value > 0.01) return true;
        }
        return false;
    }
};

void reactive_decay(ReactiveFlashState& state, double now_seconds);
void reactive_flash(ReactiveFlashState& state, int zone, Rgb color, double now_seconds);
Rgb reactive_color_for(const LightingProfile& profile, int zone, std::uint32_t seed);
std::array<Rgb, 4> reactive_render(const ReactiveFlashState& state, const LightingProfile& profile);

std::string default_lighting_config_path();
std::string default_lightd_socket_path();
std::string default_config_dir();

std::optional<std::string> read_text_file(const std::string& path);
bool write_text_file_atomic(const std::string& path, const std::string& content, std::string& error);
std::int64_t file_mtime_ns(const std::string& path);

}  // namespace legion::core
