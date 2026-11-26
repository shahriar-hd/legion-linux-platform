#pragma once

#include <functional>
#include <string>
#include <vector>

namespace legion::backends::linux_input {

// Read-only reader for the internal keyboard event devices. It never grabs the
// device, so normal typing keeps working. Physical access to /dev/input/event*
// is required; install the keyboard uaccess rule or join the input group.
class KeyboardInput {
public:
    KeyboardInput();
    ~KeyboardInput();
    KeyboardInput(const KeyboardInput&) = delete;
    KeyboardInput& operator=(const KeyboardInput&) = delete;

    bool open(std::string& error);
    void close();
    bool is_open() const;

    // Drains pending key events. Calls on_key(keycode, pressed) for each event.
    void poll(const std::function<void(int keycode, bool pressed)>& on_key);

    std::string device_summary() const;

private:
    std::vector<int> fds_;
    std::vector<std::string> paths_;
};

}  // namespace legion::backends::linux_input
