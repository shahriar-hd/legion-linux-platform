#include "legion/backends/lenovo/lenovo_keymap.hpp"

#include <linux/input-event-codes.h>

namespace legion::backends::lenovo {

int legion_key_to_zone(int keycode) {
    switch (keycode) {
        // Function row: F1-F5 -> zone 0, F6-F10 -> zone 1, F11-F12 -> zone 2.
        case KEY_ESC:
        case KEY_F1:
        case KEY_F2:
        case KEY_F3:
        case KEY_F4:
        case KEY_F5:
            return 0;
        case KEY_F6:
        case KEY_F7:
        case KEY_F8:
        case KEY_F9:
        case KEY_F10:
            return 1;
        case KEY_F11:
        case KEY_F12:
            return 2;

        // Number row: ` 1 2 3 4 | 5 6 7 8 9 | 0 - = Backspace.
        case KEY_GRAVE:
        case KEY_1:
        case KEY_2:
        case KEY_3:
        case KEY_4:
            return 0;
        case KEY_5:
        case KEY_6:
        case KEY_7:
        case KEY_8:
        case KEY_9:
            return 1;
        case KEY_0:
        case KEY_MINUS:
        case KEY_EQUAL:
        case KEY_BACKSPACE:
            return 2;

        // Letter row 1: Tab Q W E R | T Y U I O | P [ ] \.
        case KEY_TAB:
        case KEY_Q:
        case KEY_W:
        case KEY_E:
        case KEY_R:
            return 0;
        case KEY_T:
        case KEY_Y:
        case KEY_U:
        case KEY_I:
        case KEY_O:
            return 1;
        case KEY_P:
        case KEY_LEFTBRACE:
        case KEY_RIGHTBRACE:
        case KEY_BACKSLASH:
            return 2;

        // Letter row 2: Caps A S D F | G H J K | L ; ' Enter.
        case KEY_CAPSLOCK:
        case KEY_A:
        case KEY_S:
        case KEY_D:
        case KEY_F:
            return 0;
        case KEY_G:
        case KEY_H:
        case KEY_J:
        case KEY_K:
            return 1;
        case KEY_L:
        case KEY_SEMICOLON:
        case KEY_APOSTROPHE:
        case KEY_ENTER:
            return 2;

        // Letter row 3: LShift Z X C V | B N M , | . / RShift.
        case KEY_LEFTSHIFT:
        case KEY_Z:
        case KEY_X:
        case KEY_C:
        case KEY_V:
            return 0;
        case KEY_B:
        case KEY_N:
        case KEY_M:
        case KEY_COMMA:
            return 1;
        case KEY_DOT:
        case KEY_SLASH:
        case KEY_RIGHTSHIFT:
            return 2;

        // Bottom row: LCtrl LWin LAlt | Space | RAlt RCtrl RWin.
        case KEY_LEFTCTRL:
        case KEY_LEFTMETA:
        case KEY_LEFTALT:
            return 0;
        case KEY_SPACE:
            return 1;
        case KEY_RIGHTALT:
        case KEY_RIGHTCTRL:
        case KEY_RIGHTMETA:
            return 2;

        // Navigation and arrow cluster just left of the numpad.
        case KEY_DELETE:
        case KEY_UP:
        case KEY_DOWN:
        case KEY_LEFT:
        case KEY_RIGHT:
            return 2;

        // Numeric keypad (zone 4), including the shared navigation keys.
        case KEY_INSERT:
        case KEY_HOME:
        case KEY_END:
        case KEY_PAGEUP:
        case KEY_PAGEDOWN:
        case KEY_NUMLOCK:
        case KEY_KPSLASH:
        case KEY_KPASTERISK:
        case KEY_KPMINUS:
        case KEY_KPPLUS:
        case KEY_KPENTER:
        case KEY_KPDOT:
#ifdef KEY_KPCOMMA
        case KEY_KPCOMMA:
#endif
#ifdef KEY_KPEQUAL
        case KEY_KPEQUAL:
#endif
        case KEY_KP0:
        case KEY_KP1:
        case KEY_KP2:
        case KEY_KP3:
        case KEY_KP4:
        case KEY_KP5:
        case KEY_KP6:
        case KEY_KP7:
        case KEY_KP8:
        case KEY_KP9:
            return 3;

        // The function row also emits these multimedia codes.
        case KEY_MUTE:
        case KEY_VOLUMEDOWN:
        case KEY_VOLUMEUP:
        case KEY_PLAYPAUSE:
        case KEY_PREVIOUSSONG:
        case KEY_NEXTSONG:
#ifdef KEY_MICMUTE
        case KEY_MICMUTE:
#endif
            return 0;

        default:
            return -1;
    }
}

}  // namespace legion::backends::lenovo
