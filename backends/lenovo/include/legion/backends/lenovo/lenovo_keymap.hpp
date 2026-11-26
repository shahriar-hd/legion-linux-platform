#pragma once

namespace legion::backends::lenovo {

// Maps a Linux evdev key code to one of the four keyboard lighting zones
// (0 = leftmost .. 3 = numpad), or -1 when the key is not part of the deck.
// Based on the Lenovo Legion 5 Pro 16" US ANSI layout with a numeric keypad.
int legion_key_to_zone(int keycode);

}  // namespace legion::backends::lenovo
