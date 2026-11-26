#include "legion/backends/lenovo/lenovo_rgb.hpp"
#include "legion/backends/lenovo/lenovo_keymap.hpp"
#include "legion/backends/linux/keyboard_input.hpp"
#include "legion/core/lighting.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <memory>
#include <mutex>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace {

std::atomic<bool> g_running{true};
std::atomic<bool> g_reload_requested{false};
std::atomic<bool> g_preview_pending{false};
std::mutex g_preview_mutex;
std::array<legion::core::Rgb, 4> g_preview_zones{};
int g_preview_brightness = 2;

void handle_signal(int) {
    g_running.store(false);
}

double boot_seconds() {
    struct timespec ts {};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

double mono_seconds() {
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

double steady_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void log_line(const std::string& message) {
    std::time_t now = std::time(nullptr);
    std::tm local {};
    localtime_r(&now, &local);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
    std::cerr << "[" << stamp << "] legion-lightd: " << message << "\n";
}

bool apply_hardware(legion::backends::lenovo::RgbSession& session,
                    const legion::core::LightingProfile& profile,
                    std::string& error) {
    if (!session.is_open() && !session.open(error)) {
        return false;
    }
    return session.apply(profile, error);
}

class ReloadServer {
public:
    ~ReloadServer() {
        stop();
    }

    bool start(const std::string& path, std::string& error) {
        path_ = path;
        ::unlink(path_.c_str());
        fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd_ < 0) {
            error = std::string("socket: ") + std::strerror(errno);
            return false;
        }
        struct sockaddr_un address {};
        address.sun_family = AF_UNIX;
        if (path_.size() >= sizeof(address.sun_path)) {
            error = "socket path too long";
            stop();
            return false;
        }
        std::strncpy(address.sun_path, path_.c_str(), sizeof(address.sun_path) - 1);
        if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) != 0) {
            error = std::string("bind: ") + std::strerror(errno);
            stop();
            return false;
        }
        ::chmod(path_.c_str(), 0600);
        if (::listen(fd_, 4) != 0) {
            error = std::string("listen: ") + std::strerror(errno);
            stop();
            return false;
        }
        thread_ = std::thread([this]() { serve(); });
        return true;
    }

    void stop() {
        if (fd_ >= 0) {
            ::shutdown(fd_, SHUT_RDWR);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        if (!path_.empty()) {
            ::unlink(path_.c_str());
        }
    }

private:
    void serve() {
        while (g_running.load()) {
            struct pollfd pfd {};
            pfd.fd = fd_;
            pfd.events = POLLIN;
            const int ready = ::poll(&pfd, 1, 500);
            if (ready <= 0) continue;
            const int client = ::accept(fd_, nullptr, nullptr);
            if (client < 0) continue;
            char buffer[128] = {0};
            const ssize_t count = ::read(client, buffer, sizeof(buffer) - 1);
            if (count > 0) {
                std::string command(buffer, static_cast<std::size_t>(count));
                while (!command.empty() && (command.back() == '\n' || command.back() == '\r')) {
                    command.pop_back();
                }
                if (command == "reload" || command == "apply") {
                    g_reload_requested.store(true);
                    const char reply[] = "ok\n";
                    if (::write(client, reply, sizeof(reply) - 1) < 0) {}

                } else if (command.rfind("preview ", 0) == 0) {
                    std::istringstream iss(command.substr(8));
                    int values[13] = {0};
                    bool ok = true;
                    for (int i = 0; i < 13; ++i) {
                        if (!(iss >> values[i])) {
                            ok = false;
                            break;
                        }
                    }
                    if (ok) {
                        std::lock_guard<std::mutex> lock(g_preview_mutex);
                        for (int zone = 0; zone < 4; ++zone) {
                            g_preview_zones[static_cast<std::size_t>(zone)] = {
                                static_cast<std::uint8_t>(std::clamp(values[zone * 3 + 0], 0, 255)),
                                static_cast<std::uint8_t>(std::clamp(values[zone * 3 + 1], 0, 255)),
                                static_cast<std::uint8_t>(std::clamp(values[zone * 3 + 2], 0, 255))};
                        }
                        g_preview_brightness = std::clamp(values[12], 1, 2);
                        g_preview_pending.store(true);
                        const char reply[] = "ok\n";
                        if (::write(client, reply, sizeof(reply) - 1) < 0) {}
                    } else {
                        const char reply[] = "bad\n";
                        if (::write(client, reply, sizeof(reply) - 1) < 0) {}
                    }

                } else if (command == "ping") {
                    const char reply[] = "ok\n";
                    if (::write(client, reply, sizeof(reply) - 1) < 0) {}

                } else if (command == "stop") {
                    const char reply[] = "stopping\n";
                    if (::write(client, reply, sizeof(reply) - 1) < 0) {}

                    g_running.store(false);
                } else {
                    const char reply[] = "unknown\n";
                    if (::write(client, reply, sizeof(reply) - 1) < 0) {}

                }
            }
            ::close(client);
        }
    }

    int fd_ = -1;
    std::string path_;
    std::thread thread_;
};

struct Options {
    std::string config_path;
    std::string socket_path;
    bool once = false;
    bool verbose = false;
    bool dry_run = false;
};

Options parse_options(int argc, char* argv[]) {
    Options options;
    options.config_path = legion::core::default_lighting_config_path();
    options.socket_path = legion::core::default_lightd_socket_path();
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            options.config_path = argv[++i];
        } else if (arg == "--socket" && i + 1 < argc) {
            options.socket_path = argv[++i];
        } else if (arg == "--once") {
            options.once = true;
        } else if (arg == "--verbose" || arg == "-v") {
            options.verbose = true;
        } else if (arg == "--dry-run") {
            options.dry_run = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: legion-lightd [--config PATH] [--socket PATH] [--once] [--verbose] [--dry-run]\n";
            std::exit(0);
        }
    }
    return options;
}

}  // namespace

int main(int argc, char* argv[]) {
    const Options options = parse_options(argc, argv);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    auto session = std::make_unique<legion::backends::lenovo::RgbSession>();
    legion::backends::linux_input::KeyboardInput keyboard;
    legion::core::ReactiveFlashState reactive_state;
    legion::core::LightingConfig config;
    bool have_config = false;
    bool armed = false;
    std::int64_t last_mtime = 0;
    double animation_start = steady_seconds();
    std::string last_error;
    std::string last_keyboard_error;
    double last_keyboard_attempt = 0.0;

    auto load_config = [&]() {
        const auto text = legion::core::read_text_file(options.config_path);
        if (!text.has_value()) return false;
        const auto parsed = legion::core::parse_lighting_config(*text);
        if (!parsed.has_value()) {
            log_line("config is empty or invalid; keeping previous state");
            return false;
        }
        const bool first_load = !have_config;
        config = *parsed;
        have_config = true;
        armed = first_load ? config.apply_on_login : true;
        animation_start = steady_seconds();
        last_mtime = legion::core::file_mtime_ns(options.config_path);
        const auto& profile = config.profiles[static_cast<std::size_t>(config.active_profile)];
        log_line("loaded profile " + std::to_string(config.active_profile + 1) + " (" +
                 legion::core::to_string(profile.effect) + ")");
        return true;
    };

    const bool loaded = load_config();
    if (!loaded && !options.once) {
        log_line("no usable config at " + options.config_path + "; waiting for one");
        armed = false;
    }

    if (options.once) {
        if (!loaded) {
            return 0;
        }
        const auto& profile = config.profiles[static_cast<std::size_t>(config.active_profile)];
        if (options.dry_run) {
            log_line("dry-run: would apply " + legion::core::to_string(profile.effect) +
                     " (profile " + std::to_string(config.active_profile + 1) + ")");
            return 0;
        }
        std::string error;
        if (!apply_hardware(*session, profile, error)) {
            log_line("apply failed: " + error);
            return 1;
        }
        log_line("applied " + legion::core::to_string(profile.effect) + " on " + session->device_summary());
        return 0;
    }

    ReloadServer server;
    std::string server_error;
    if (!server.start(options.socket_path, server_error)) {
        log_line("reload socket disabled: " + server_error);
    } else if (options.verbose) {
        log_line("reload socket listening at " + options.socket_path);
    }

    double baseline_offset = boot_seconds() - mono_seconds();
    double last_config_check = 0.0;
    auto last_hardware_apply = std::chrono::steady_clock::now();
    bool need_apply = armed;

    while (g_running.load()) {
        const double now_boot = boot_seconds();
        const double now_mono = mono_seconds();
        if (now_boot - now_mono - baseline_offset > 2.0) {
            baseline_offset = now_boot - now_mono;
            need_apply = true;
            log_line("suspend/resume detected; reapplying profile");
        }

        const double now_steady = steady_seconds();
        if (now_steady - last_config_check >= 0.5) {
            last_config_check = now_steady;
            const std::int64_t mtime = legion::core::file_mtime_ns(options.config_path);
            const bool reload = g_reload_requested.exchange(false);
            if (mtime != last_mtime || reload) {
                if (load_config()) {
                    need_apply = true;
                } else {
                    last_mtime = mtime;
                }
            }
        }

        if (!have_config || !armed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        const auto& profile = config.profiles[static_cast<std::size_t>(config.active_profile)];
        if (options.dry_run) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        if (g_preview_pending.exchange(false)) {
            std::array<legion::core::Rgb, 4> zones{};
            int brightness = 2;
            {
                std::lock_guard<std::mutex> lock(g_preview_mutex);
                zones = g_preview_zones;
                brightness = g_preview_brightness;
            }
            if (!session->is_open()) {
                std::string error;
                session->open(error);
            }
            if (session->is_open()) {
                std::string error;
                session->apply_zones(zones, brightness, error);
            }
            last_hardware_apply = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            continue;
        }

        if (legion::core::is_software_effect(profile.effect)) {
            if (!session->is_open()) {
                std::string error;
                if (!session->open(error)) {
                    if (error != last_error) {
                        log_line("waiting for RGB device: " + error);
                        last_error = error;
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
            }
            const double elapsed = steady_seconds() - animation_start;
            std::array<legion::core::Rgb, 4> frame{};
            if (profile.effect == legion::core::LightingEffect::Reactive) {
                if (!keyboard.is_open() && elapsed - last_keyboard_attempt > 2.0) {
                    last_keyboard_attempt = elapsed;
                    std::string key_error;
                    if (!keyboard.open(key_error)) {
                        if (key_error != last_keyboard_error) {
                            log_line("reactive input unavailable: " + key_error);
                            last_keyboard_error = key_error;
                        }
                    } else {
                        log_line("reactive input reading " + keyboard.device_summary());
                        last_keyboard_error.clear();
                    }
                }
                keyboard.poll([&](int code, bool pressed) {
                    if (!pressed) return;
                    const int zone = legion::backends::lenovo::legion_key_to_zone(code);
                    if (zone < 0) return;
                    const auto seed = static_cast<std::uint32_t>(elapsed * 1000.0) +
                                      static_cast<std::uint32_t>(code) * 2654435761U;
                    const legion::core::Rgb color =
                        legion::core::reactive_color_for(profile, zone, seed);
                    legion::core::reactive_flash(reactive_state, zone, color, elapsed);
                });
                legion::core::reactive_decay(reactive_state, elapsed);
                frame = legion::core::reactive_render(reactive_state, profile);
            } else {
                frame = legion::core::sample_lighting_frame(profile.effect, profile, elapsed);
            }
            std::string error;
            if (!session->apply_zones(frame, profile.brightness, error)) {
                if (error != last_error) {
                    log_line("frame write failed: " + error);
                    last_error = error;
                }
                session = std::make_unique<legion::backends::lenovo::RgbSession>();
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            last_error.clear();
            need_apply = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        } else if (need_apply) {
            std::string error;
            if (apply_hardware(*session, profile, error)) {
                log_line("applied " + legion::core::to_string(profile.effect) + " on " +
                         session->device_summary());
                need_apply = false;
                last_hardware_apply = std::chrono::steady_clock::now();
                last_error.clear();
            } else {
                if (error != last_error) {
                    log_line("apply failed: " + error);
                    last_error = error;
                }
                session = std::make_unique<legion::backends::lenovo::RgbSession>();
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        } else {
            if (profile.effect == legion::core::LightingEffect::Static) {
                const auto since = std::chrono::steady_clock::now() - last_hardware_apply;
                if (since > std::chrono::seconds(10)) {
                    need_apply = true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }

    server.stop();
    log_line("stopped");
    return 0;
}
