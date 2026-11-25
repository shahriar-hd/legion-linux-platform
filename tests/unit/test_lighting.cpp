#include "legion/backends/lenovo/lenovo_rgb.hpp"
#include "legion/core/lighting.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

namespace {

void test_config_round_trip() {
    legion::core::LightingConfig config;
    config.active_profile = 2;
    config.apply_on_login = false;
    config.profiles[0].name = "Night Wave";
    config.profiles[0].effect = legion::core::LightingEffect::Wave;
    config.profiles[0].direction = legion::core::WaveDirection::RightToLeft;
    config.profiles[0].speed = 4;
    config.profiles[0].brightness = 1;
    config.profiles[1].effect = legion::core::LightingEffect::Static;
    config.profiles[1].zones[0] = {10, 20, 30};
    config.profiles[2].effect = legion::core::LightingEffect::Heartbeat;
    config.profiles[3].effect = legion::core::LightingEffect::Dance;

    const std::string text = legion::core::serialize_lighting_config(config);
    const auto parsed = legion::core::parse_lighting_config(text);
    assert(parsed.has_value());
    assert(parsed->active_profile == 2);
    assert(parsed->apply_on_login == false);
    assert(parsed->profiles[0].name == "Night Wave");
    assert(parsed->profiles[0].effect == legion::core::LightingEffect::Wave);
    assert(parsed->profiles[0].direction == legion::core::WaveDirection::RightToLeft);
    assert(parsed->profiles[0].speed == 4);
    assert(parsed->profiles[0].brightness == 1);
    assert(parsed->profiles[1].zones[0].r == 10);
    assert(parsed->profiles[1].zones[0].g == 20);
    assert(parsed->profiles[1].zones[0].b == 30);
    assert(parsed->profiles[2].effect == legion::core::LightingEffect::Heartbeat);
    assert(parsed->profiles[3].effect == legion::core::LightingEffect::Dance);
}

void test_effect_aliases_and_paths() {
    legion::core::LightingEffect effect = legion::core::LightingEffect::Off;
    assert(legion::core::parse_lighting_effect("breath", effect));
    assert(effect == legion::core::LightingEffect::Breathing);
    assert(legion::core::parse_lighting_effect("hue", effect));
    assert(effect == legion::core::LightingEffect::Smooth);
    assert(legion::core::parse_lighting_effect("disco", effect));
    assert(effect == legion::core::LightingEffect::Dance);
    assert(!legion::core::parse_lighting_effect("nonsense", effect));

    assert(legion::core::is_hardware_effect(legion::core::LightingEffect::Static));
    assert(legion::core::is_hardware_effect(legion::core::LightingEffect::Smooth));
    assert(!legion::core::is_hardware_effect(legion::core::LightingEffect::Rainbow));
    assert(legion::core::is_software_effect(legion::core::LightingEffect::Heartbeat));
    assert(!legion::core::parse_lighting_config("").has_value());
    assert(!legion::core::parse_lighting_config("not a lighting config").has_value());
}

void test_static_report_layout() {
    legion::core::LightingProfile profile;
    profile.effect = legion::core::LightingEffect::Static;
    profile.speed = 3;
    profile.brightness = 2;
    profile.zones = {legion::core::Rgb{1, 2, 3}, legion::core::Rgb{4, 5, 6},
                     legion::core::Rgb{7, 8, 9}, legion::core::Rgb{10, 11, 12}};

    const auto report = legion::backends::lenovo::build_rgb_report(profile);
    assert(report.valid);
    assert(report.bytes[0] == 0xCC);
    assert(report.bytes[1] == 0x16);
    assert(report.bytes[2] == 0x01);
    assert(report.bytes[3] == 3);
    assert(report.bytes[4] == 2);
    assert(report.bytes[5] == 1 && report.bytes[6] == 2 && report.bytes[7] == 3);
    assert(report.bytes[8] == 4 && report.bytes[9] == 5 && report.bytes[10] == 6);
    assert(report.bytes[11] == 7 && report.bytes[12] == 8 && report.bytes[13] == 9);
    assert(report.bytes[14] == 10 && report.bytes[15] == 11 && report.bytes[16] == 12);
    assert(report.bytes[18] == 0 && report.bytes[19] == 0);
}

void test_wave_direction_and_off() {
    legion::core::LightingProfile wave;
    wave.effect = legion::core::LightingEffect::Wave;
    wave.speed = 2;
    wave.brightness = 2;
    wave.direction = legion::core::WaveDirection::LeftToRight;
    auto report = legion::backends::lenovo::build_rgb_report(wave);
    assert(report.valid);
    assert(report.bytes[2] == 0x04);
    assert(report.bytes[18] == 0x01);
    assert(report.bytes[19] == 0x00);

    wave.direction = legion::core::WaveDirection::RightToLeft;
    report = legion::backends::lenovo::build_rgb_report(wave);
    assert(report.valid);
    assert(report.bytes[18] == 0x00);
    assert(report.bytes[19] == 0x01);

    legion::core::LightingProfile off;
    off.effect = legion::core::LightingEffect::Off;
    report = legion::backends::lenovo::build_rgb_report(off);
    assert(report.valid);
    assert(report.bytes[2] == 0x00);

    legion::core::LightingProfile smooth;
    smooth.effect = legion::core::LightingEffect::Smooth;
    report = legion::backends::lenovo::build_rgb_report(smooth);
    assert(report.valid);
    assert(report.bytes[2] == 0x06);
}

void test_invalid_reports() {
    legion::core::LightingProfile profile;
    profile.effect = legion::core::LightingEffect::Static;
    profile.speed = 0;
    auto report = legion::backends::lenovo::build_rgb_report(profile);
    assert(!report.valid);

    profile.speed = 2;
    profile.brightness = 0;
    report = legion::backends::lenovo::build_rgb_report(profile);
    assert(!report.valid);

    profile.brightness = 2;
    profile.effect = legion::core::LightingEffect::Rainbow;
    report = legion::backends::lenovo::build_rgb_report(profile);
    assert(!report.valid);
}

void test_frame_sampler() {
    legion::core::LightingProfile profile;
    profile.speed = 2;
    profile.brightness = 2;
    profile.zones = {legion::core::Rgb{255, 0, 0}, legion::core::Rgb{0, 255, 0},
                     legion::core::Rgb{0, 0, 255}, legion::core::Rgb{255, 255, 255}};

    auto static_frame = legion::core::sample_lighting_frame(
        legion::core::LightingEffect::Static, profile, 1.0);
    assert(static_frame[0].r == 255 && static_frame[0].g == 0);
    assert(static_frame[3].r == 255 && static_frame[3].g == 255 && static_frame[3].b == 255);

    auto off_frame = legion::core::sample_lighting_frame(
        legion::core::LightingEffect::Off, profile, 1.0);
    for (const auto& zone : off_frame) {
        assert(zone.r == 0 && zone.g == 0 && zone.b == 0);
    }

    auto rainbow_a = legion::core::sample_lighting_frame(
        legion::core::LightingEffect::Rainbow, profile, 0.0);
    auto rainbow_b = legion::core::sample_lighting_frame(
        legion::core::LightingEffect::Rainbow, profile, 0.4);
    bool changed = false;
    for (std::size_t i = 0; i < rainbow_a.size(); ++i) {
        if (rainbow_a[i].r != rainbow_b[i].r || rainbow_a[i].g != rainbow_b[i].g ||
            rainbow_a[i].b != rainbow_b[i].b) {
            changed = true;
        }
    }
    assert(changed);

    auto dance_a = legion::core::sample_lighting_frame(
        legion::core::LightingEffect::Dance, profile, 1.0);
    auto dance_b = legion::core::sample_lighting_frame(
        legion::core::LightingEffect::Dance, profile, 1.0);
    for (std::size_t i = 0; i < dance_a.size(); ++i) {
        assert(dance_a[i].r == dance_b[i].r);
        assert(dance_a[i].g == dance_b[i].g);
        assert(dance_a[i].b == dance_b[i].b);
    }

    auto reactive = legion::core::sample_lighting_frame(
        legion::core::LightingEffect::Reactive, profile, 1.0);
    assert(reactive[0].r < profile.zones[0].r);
}

void test_hsv() {
    const auto red = legion::core::hsv_to_rgb(0.0, 1.0, 1.0);
    assert(red.r == 255 && red.g == 0 && red.b == 0);
    const auto green = legion::core::hsv_to_rgb(1.0 / 3.0, 1.0, 1.0);
    assert(green.r == 0 && green.g == 255 && green.b == 0);
    const auto blue = legion::core::hsv_to_rgb(2.0 / 3.0, 1.0, 1.0);
    assert(blue.r == 0 && blue.g == 0 && blue.b == 255);
}

}  // namespace

int main() {
    test_config_round_trip();
    test_effect_aliases_and_paths();
    test_static_report_layout();
    test_wave_direction_and_off();
    test_invalid_reports();
    test_frame_sampler();
    test_hsv();
    std::cout << "All lighting tests passed.\n";
    return 0;
}
