#include "legion/core/lighting.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace legion::core {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

int clamp_int(int value, int lo, int hi) {
    return std::max(lo, std::min(hi, value));
}

std::uint8_t clamp_byte(int value) {
    return static_cast<std::uint8_t>(clamp_int(value, 0, 255));
}

std::uint32_t hash32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

Rgb palette_color(std::uint32_t index) {
    static const Rgb palette[] = {
        {255, 0, 64}, {255, 128, 0}, {255, 230, 0}, {0, 230, 118},
        {0, 212, 255}, {64, 96, 255}, {180, 0, 255}, {255, 0, 180}
    };
    return palette[index % (sizeof(palette) / sizeof(palette[0]))];
}

Rgb scale_color(Rgb color, double factor) {
    factor = std::clamp(factor, 0.0, 1.0);
    return {static_cast<std::uint8_t>(std::lround(color.r * factor)),
            static_cast<std::uint8_t>(std::lround(color.g * factor)),
            static_cast<std::uint8_t>(std::lround(color.b * factor))};
}

Rgb add_clamp(Rgb a, Rgb b) {
    return {clamp_byte(static_cast<int>(a.r) + static_cast<int>(b.r)),
            clamp_byte(static_cast<int>(a.g) + static_cast<int>(b.g)),
            clamp_byte(static_cast<int>(a.b) + static_cast<int>(b.b))};
}

Rgb average_color(const std::array<Rgb, 4>& zones) {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    for (const auto& zone : zones) {
        r += zone.r;
        g += zone.g;
        b += zone.b;
    }
    return {clamp_byte(static_cast<int>(std::lround(r / 4.0))),
            clamp_byte(static_cast<int>(std::lround(g / 4.0))),
            clamp_byte(static_cast<int>(std::lround(b / 4.0)))};
}

double heartbeat_pulse(double phase) {
    auto gaussian = [](double x, double center, double width) {
        const double d = (x - center) / width;
        return std::exp(-0.5 * d * d);
    };
    double p = phase - std::floor(phase);
    const double lub = gaussian(p, 0.06, 0.045);
    const double dub = 0.62 * gaussian(p, 0.22, 0.05);
    return std::clamp(lub + dub, 0.0, 1.0);
}

}  // namespace

LightingConfig::LightingConfig() {
    for (int i = 0; i < 4; ++i) {
        profiles[static_cast<std::size_t>(i)].name = "Profile " + std::to_string(i + 1);
        profiles[static_cast<std::size_t>(i)].speed = 2;
        profiles[static_cast<std::size_t>(i)].brightness = 2;
    }
    profiles[0].effect = LightingEffect::Wave;
    profiles[0].direction = WaveDirection::LeftToRight;
    profiles[1].effect = LightingEffect::Static;
    profiles[2].effect = LightingEffect::Smooth;
    profiles[3].effect = LightingEffect::Breathing;
    for (auto& profile : profiles) {
        for (auto& zone : profile.zones) {
            zone = Rgb{0, 212, 255};
        }
    }
    profiles[3].zones = {Rgb{255, 45, 149}, Rgb{255, 45, 149}, Rgb{255, 45, 149}, Rgb{255, 45, 149}};
}

std::string to_string(LightingEffect effect) {
    switch (effect) {
        case LightingEffect::Off: return "off";
        case LightingEffect::Static: return "static";
        case LightingEffect::Breathing: return "breathing";
        case LightingEffect::Wave: return "wave";
        case LightingEffect::Smooth: return "smooth";
        case LightingEffect::Heartbeat: return "heartbeat";
        case LightingEffect::Rainbow: return "rainbow";
        case LightingEffect::Dance: return "dance";
        case LightingEffect::Random: return "random";
        case LightingEffect::Reactive: return "reactive";
    }
    return "static";
}

std::string to_string(WaveDirection direction) {
    return direction == WaveDirection::LeftToRight ? "ltr" : "rtl";
}

std::string to_string(ReactiveMode mode) {
    return mode == ReactiveMode::Random ? "random" : "preset";
}

bool parse_lighting_effect(const std::string& text, LightingEffect& out) {
    const std::string value = trim(text);
    if (value == "off") out = LightingEffect::Off;
    else if (value == "static" || value == "solid") out = LightingEffect::Static;
    else if (value == "breathing" || value == "breath") out = LightingEffect::Breathing;
    else if (value == "wave") out = LightingEffect::Wave;
    else if (value == "smooth" || value == "hue" || value == "spectrum") out = LightingEffect::Smooth;
    else if (value == "heartbeat") out = LightingEffect::Heartbeat;
    else if (value == "rainbow") out = LightingEffect::Rainbow;
    else if (value == "dance" || value == "disco") out = LightingEffect::Dance;
    else if (value == "random") out = LightingEffect::Random;
    else if (value == "reactive") out = LightingEffect::Reactive;
    else return false;
    return true;
}

bool parse_wave_direction(const std::string& text, WaveDirection& out) {
    const std::string value = trim(text);
    if (value == "ltr" || value == "left-to-right" || value == "right") {
        out = WaveDirection::LeftToRight;
        return true;
    }
    if (value == "rtl" || value == "right-to-left" || value == "left") {
        out = WaveDirection::RightToLeft;
        return true;
    }
    return false;
}

bool parse_reactive_mode(const std::string& text, ReactiveMode& out) {
    const std::string value = trim(text);
    if (value == "preset" || value == "static" || value == "color") {
        out = ReactiveMode::Preset;
        return true;
    }
    if (value == "random" || value == "rainbow") {
        out = ReactiveMode::Random;
        return true;
    }
    return false;
}

bool is_hardware_effect(LightingEffect effect) {
    switch (effect) {
        case LightingEffect::Off:
        case LightingEffect::Static:
        case LightingEffect::Breathing:
        case LightingEffect::Wave:
        case LightingEffect::Smooth:
            return true;
        default:
            return false;
    }
}

bool is_software_effect(LightingEffect effect) {
    return !is_hardware_effect(effect);
}

bool effect_uses_zones(LightingEffect effect) {
    switch (effect) {
        case LightingEffect::Static:
        case LightingEffect::Breathing:
        case LightingEffect::Heartbeat:
        case LightingEffect::Reactive:
            return true;
        default:
            return false;
    }
}

bool effect_uses_direction(LightingEffect effect) {
    return effect == LightingEffect::Wave;
}

std::string serialize_lighting_config(const LightingConfig& config) {
    std::ostringstream out;
    out << "version=1\n";
    out << "active_profile=" << clamp_int(config.active_profile, 0, 3) << "\n";
    out << "apply_on_login=" << (config.apply_on_login ? 1 : 0) << "\n";
    for (std::size_t i = 0; i < config.profiles.size(); ++i) {
        const auto& profile = config.profiles[i];
        std::string name = profile.name;
        std::replace(name.begin(), name.end(), '\n', ' ');
        std::replace(name.begin(), name.end(), '\r', ' ');
        out << "profile." << i << ".name=" << name << "\n";
        out << "profile." << i << ".effect=" << to_string(profile.effect) << "\n";
        out << "profile." << i << ".speed=" << clamp_int(profile.speed, 1, 4) << "\n";
        out << "profile." << i << ".brightness=" << clamp_int(profile.brightness, 1, 2) << "\n";
        out << "profile." << i << ".direction=" << to_string(profile.direction) << "\n";
        out << "profile." << i << ".reactive=" << to_string(profile.reactive_mode) << "\n";
        for (std::size_t z = 0; z < profile.zones.size(); ++z) {
            const auto& zone = profile.zones[z];
            out << "profile." << i << ".zone" << z << "="
                << static_cast<int>(zone.r) << ","
                << static_cast<int>(zone.g) << ","
                << static_cast<int>(zone.b) << "\n";
        }
    }
    return out.str();
}

std::optional<LightingConfig> parse_lighting_config(const std::string& text) {
    if (trim(text).empty()) {
        return std::nullopt;
    }

    LightingConfig config;
    bool recognized = false;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));

        if (key == "active_profile") {
            recognized = true;
            try {
                config.active_profile = clamp_int(std::stoi(value), 0, 3);
            } catch (...) {
            }
        } else if (key == "apply_on_login") {
            recognized = true;
            config.apply_on_login = value != "0" && value != "false";
        } else if (key.rfind("profile.", 0) == 0) {
            recognized = true;
            const auto field_dot = key.find('.', 8);
            if (field_dot == std::string::npos) continue;
            int index = -1;
            try {
                index = std::stoi(key.substr(8, field_dot - 8));
            } catch (...) {
                continue;
            }
            if (index < 0 || index > 3) continue;
            const std::string field = key.substr(field_dot + 1);
            auto& profile = config.profiles[static_cast<std::size_t>(index)];

            if (field == "name") {
                profile.name = value;
            } else if (field == "effect") {
                LightingEffect effect = profile.effect;
                if (parse_lighting_effect(value, effect)) profile.effect = effect;
            } else if (field == "speed") {
                try {
                    profile.speed = clamp_int(std::stoi(value), 1, 4);
                } catch (...) {
                }
            } else if (field == "brightness") {
                try {
                    profile.brightness = clamp_int(std::stoi(value), 1, 2);
                } catch (...) {
                }
            } else if (field == "direction") {
                WaveDirection direction = profile.direction;
                if (parse_wave_direction(value, direction)) profile.direction = direction;
            } else if (field == "reactive") {
                ReactiveMode mode = profile.reactive_mode;
                if (parse_reactive_mode(value, mode)) profile.reactive_mode = mode;
            } else if (field.rfind("zone", 0) == 0) {
                try {
                    const int zone_index = std::stoi(field.substr(4));
                    if (zone_index < 0 || zone_index > 3) continue;
                    std::istringstream values(value);
                    std::string token;
                    int rgb[3] = {0, 0, 0};
                    int count = 0;
                    while (count < 3 && std::getline(values, token, ',')) {
                        rgb[count++] = std::stoi(trim(token));
                    }
                    if (count == 3) {
                        profile.zones[static_cast<std::size_t>(zone_index)] = {
                            clamp_byte(rgb[0]), clamp_byte(rgb[1]), clamp_byte(rgb[2])};
                    }
                } catch (...) {
                }
            }
        }
    }
    if (!recognized) {
        return std::nullopt;
    }
    return config;
}

Rgb hsv_to_rgb(double hue, double saturation, double value) {
    hue = hue - std::floor(hue);
    saturation = std::clamp(saturation, 0.0, 1.0);
    value = std::clamp(value, 0.0, 1.0);
    const double h = hue * 6.0;
    const int sector = static_cast<int>(std::floor(h)) % 6;
    const double f = h - std::floor(h);
    const double p = value * (1.0 - saturation);
    const double q = value * (1.0 - saturation * f);
    const double t = value * (1.0 - saturation * (1.0 - f));
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    switch (sector) {
        case 0: r = value; g = t; b = p; break;
        case 1: r = q; g = value; b = p; break;
        case 2: r = p; g = value; b = t; break;
        case 3: r = p; g = q; b = value; break;
        case 4: r = t; g = p; b = value; break;
        default: r = value; g = p; b = q; break;
    }
    return {clamp_byte(static_cast<int>(std::lround(r * 255.0))),
            clamp_byte(static_cast<int>(std::lround(g * 255.0))),
            clamp_byte(static_cast<int>(std::lround(b * 255.0)))};
}

std::array<Rgb, 4> sample_lighting_frame(LightingEffect effect,
                                         const LightingProfile& profile,
                                         double time_seconds) {
    std::array<Rgb, 4> frame{};
    const double speed = static_cast<double>(clamp_int(profile.speed, 1, 4));
    const double rate = 0.25 + 0.18 * speed;
    const double t = std::max(0.0, time_seconds);

    switch (effect) {
        case LightingEffect::Off:
            break;
        case LightingEffect::Static:
            frame = profile.zones;
            break;
        case LightingEffect::Breathing: {
            const double intensity = 0.10 + 0.90 * (0.5 - 0.5 * std::cos(2.0 * kPi * t * rate));
            for (std::size_t i = 0; i < frame.size(); ++i) {
                frame[i] = scale_color(profile.zones[i], intensity);
            }
            break;
        }
        case LightingEffect::Wave: {
            const double sign = profile.direction == WaveDirection::LeftToRight ? -1.0 : 1.0;
            for (std::size_t i = 0; i < frame.size(); ++i) {
                const double hue = t * rate * 0.35 + sign * static_cast<double>(i) * 0.18;
                frame[i] = hsv_to_rgb(hue, 1.0, 1.0);
            }
            break;
        }
        case LightingEffect::Smooth: {
            const double hue = t * rate * 0.30;
            for (auto& zone : frame) {
                zone = hsv_to_rgb(hue, 1.0, 1.0);
            }
            break;
        }
        case LightingEffect::Heartbeat: {
            const double beats_per_second = 0.55 + 0.18 * speed;
            const double intensity = heartbeat_pulse(t * beats_per_second);
            const Rgb base = average_color(profile.zones);
            const Rgb color = (base.r + base.g + base.b == 0) ? Rgb{255, 0, 40} : base;
            const Rgb pulsed = scale_color(color, 0.12 + 0.88 * intensity);
            for (auto& zone : frame) {
                zone = pulsed;
            }
            break;
        }
        case LightingEffect::Rainbow: {
            // A seamless 4-zone slice of the hue wheel that shifts continuously.
            // The 1/8 spacing matches the red|orange|yellow|green|cyan|blue|purple cycle.
            const double shift = t * rate * 0.16;
            const double spacing = 0.125;
            for (std::size_t i = 0; i < frame.size(); ++i) {
                frame[i] = hsv_to_rgb(shift - static_cast<double>(i) * spacing, 1.0, 1.0);
            }
            break;
        }
        case LightingEffect::Dance: {
            const double beats_per_second = 1.6 + 0.8 * speed;
            const auto beat = static_cast<std::uint32_t>(std::floor(t * beats_per_second));
            for (std::size_t i = 0; i < frame.size(); ++i) {
                const auto seed = hash32(beat * 4U + static_cast<std::uint32_t>(i));
                frame[i] = palette_color(seed);
            }
            break;
        }
        case LightingEffect::Random: {
            const double changes_per_second = 0.35 + 0.20 * speed;
            const auto step = static_cast<std::uint32_t>(std::floor(t * changes_per_second));
            for (std::size_t i = 0; i < frame.size(); ++i) {
                const auto seed = hash32(step * 7U + static_cast<std::uint32_t>(i) * 131U);
                frame[i] = palette_color(seed);
            }
            break;
        }
        case LightingEffect::Reactive: {
            for (std::size_t i = 0; i < frame.size(); ++i) {
                frame[i] = scale_color(profile.zones[i], 0.06);
            }
            break;
        }
    }
    return frame;
}

void reactive_decay(ReactiveFlashState& state, double now_seconds) {
    if (state.last_time == 0.0) {
        state.last_time = now_seconds;
        return;
    }
    double dt = now_seconds - state.last_time;
    if (dt < 0.0) dt = 0.0;
    state.last_time = now_seconds;
    const double factor = std::exp(-dt / 0.16);
    for (double& intensity : state.intensity) {
        intensity *= factor;
        if (intensity < 0.002) intensity = 0.0;
    }
}

void reactive_flash(ReactiveFlashState& state, int zone, Rgb color, double now_seconds) {
    if (zone < 0 || zone > 3) return;
    reactive_decay(state, now_seconds);
    state.intensity[static_cast<std::size_t>(zone)] = 1.0;
    state.color[static_cast<std::size_t>(zone)] = color;
}

Rgb reactive_color_for(const LightingProfile& profile, int zone, std::uint32_t seed) {
    if (profile.reactive_mode == ReactiveMode::Preset) {
        if (zone < 0 || zone > 3) return {255, 255, 255};
        const Rgb preset = profile.zones[static_cast<std::size_t>(zone)];
        if (preset.r + preset.g + preset.b == 0) return {255, 255, 255};
        return preset;
    }
    const double hue = static_cast<double>(hash32(seed) % 3600U) / 3600.0;
    return hsv_to_rgb(hue, 1.0, 1.0);
}

std::array<Rgb, 4> reactive_render(const ReactiveFlashState& state, const LightingProfile& profile) {
    std::array<Rgb, 4> frame{};
    for (int i = 0; i < 4; ++i) {
        const std::size_t index = static_cast<std::size_t>(i);
        const Rgb base = scale_color(profile.zones[index], 0.06);
        const Rgb flash = scale_color(state.color[index], std::clamp(state.intensity[index], 0.0, 1.0));
        frame[index] = add_clamp(base, flash);
    }
    return frame;
}

std::string default_config_dir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && xdg[0] != '\0') {
        return std::string(xdg) + "/legion-linux-platform";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0') {
        return std::string(home) + "/.config/legion-linux-platform";
    }
    return "/tmp/legion-linux-platform";
}

std::string default_lighting_config_path() {
    return default_config_dir() + "/lighting.conf";
}

std::string default_lightd_socket_path() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime != nullptr && runtime[0] != '\0') {
        return std::string(runtime) + "/legion-lightd.sock";
    }
    return "/tmp/legion-lightd-" + std::to_string(static_cast<long>(getuid())) + ".sock";
}

std::optional<std::string> read_text_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return std::nullopt;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad()) return std::nullopt;
    return buffer.str();
}

bool write_text_file_atomic(const std::string& path, const std::string& content, std::string& error) {
    std::error_code ec;
    const std::filesystem::path target(path);
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) {
            error = "cannot create directory: " + ec.message();
            return false;
        }
    }
    const std::string temporary = path + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            error = "cannot open temporary file";
            return false;
        }
        file << content;
        file.flush();
        if (!file.good()) {
            error = "write failed";
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        error = "rename failed: " + ec.message();
        return false;
    }
    return true;
}

std::int64_t file_mtime_ns(const std::string& path) {
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0) return 0;
    return static_cast<std::int64_t>(info.st_mtim.tv_sec) * 1000000000LL +
           static_cast<std::int64_t>(info.st_mtim.tv_nsec);
}

}  // namespace legion::core
