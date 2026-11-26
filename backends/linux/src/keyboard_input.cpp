#include "legion/backends/linux/keyboard_input.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <linux/input.h>
#include <set>
#include <sys/ioctl.h>
#include <unistd.h>

namespace legion::backends::linux_input {

namespace {

constexpr int kEvKey = EV_KEY;

bool has_key(int fd, unsigned long code) {
    unsigned long bits[(KEY_MAX / (8U * sizeof(unsigned long))) + 2] = {0};
    if (::ioctl(fd, EVIOCGBIT(kEvKey, sizeof(bits)), bits) < 0) return false;
    const unsigned long word = code / (8U * sizeof(unsigned long));
    const unsigned long bit = code % (8U * sizeof(unsigned long));
    return (bits[word] & (1UL << bit)) != 0;
}

}  // namespace

KeyboardInput::KeyboardInput() = default;

KeyboardInput::~KeyboardInput() {
    close();
}

bool KeyboardInput::open(std::string& error) {
    close();
    std::error_code ec;
    const std::filesystem::path base("/dev/input");
    if (!std::filesystem::exists(base, ec)) {
        error = "/dev/input is not available";
        return false;
    }
    bool permission_denied = false;
    for (const auto& entry : std::filesystem::directory_iterator(base, ec)) {
        if (ec) break;
        const std::string name = entry.path().filename().string();
        if (name.rfind("event", 0) != 0) continue;
        const int fd = ::open(entry.path().c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            if (errno == EACCES) permission_denied = true;
            continue;
        }
        // A keyboard exposes the letter keys; a mouse or button device does not.
        if (!has_key(fd, KEY_A) || !has_key(fd, KEY_Z) || !has_key(fd, KEY_ENTER)) {
            ::close(fd);
            continue;
        }
        fds_.push_back(fd);
        paths_.push_back(entry.path().string());
    }
    if (fds_.empty()) {
        error = permission_denied
            ? "keyboard devices are not readable (install the reactive-lighting udev rule or join the input group)"
            : "no keyboard input device found";
        return false;
    }
    return true;
}

void KeyboardInput::close() {
    for (int fd : fds_) {
        if (fd >= 0) ::close(fd);
    }
    fds_.clear();
    paths_.clear();
}

bool KeyboardInput::is_open() const {
    return !fds_.empty();
}

void KeyboardInput::poll(const std::function<void(int keycode, bool pressed)>& on_key) {
    if (!on_key) return;
    std::set<int> pressed_codes;
    for (int fd : fds_) {
        struct input_event events[32];
        while (true) {
            const ssize_t count = ::read(fd, events, sizeof(events));
            if (count <= 0) break;
            const ssize_t n = count / static_cast<ssize_t>(sizeof(struct input_event));
            for (ssize_t i = 0; i < n; ++i) {
                if (events[i].type != kEvKey) continue;
                if (events[i].value == 1) {
                    pressed_codes.insert(static_cast<int>(events[i].code));
                }
            }
        }
    }
    for (int code : pressed_codes) {
        on_key(code, true);
    }
}

std::string KeyboardInput::device_summary() const {
    std::string summary;
    for (std::size_t i = 0; i < paths_.size(); ++i) {
        if (i != 0) summary += ", ";
        summary += paths_[i];
    }
    return summary;
}

}  // namespace legion::backends::linux_input
