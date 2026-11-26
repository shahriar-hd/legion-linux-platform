# Lenovo ITE 8295 4-Zone RGB Protocol

Target controller: USB `048d:c965` (ITE Device 8295), also seen as `c955`, `c963`, `c973`, `c975`, `c983`–`c985`, `c993`–`c995`.

## Transport

- 33-byte HID feature report.
- Report ID `0xCC`.
- Linux: `HIDIOCSFEATURE` on the matching `/dev/hidraw*` node; the kernel HID driver stays attached.
- The report ID is also byte 0 of the report.

## Report layout

```text
[0]      0xCC        report ID
[1]      0x16        constant header
[2]      effect      0x00 off | 0x01 static | 0x03 breathing | 0x04 wave | 0x06 smooth/hue
[3]      speed       1..4
[4]      brightness  1 low | 2 high
[5..16]  4 zones × R,G,B, left to right (static and breathing only)
[17]     padding     0x00
[18]     wave RTL    0x01 when the wave scrolls right to left
[19]     wave LTR    0x01 when the wave scrolls left to right
[20..32] unused      0x00
```

Notes:

- Brightness is `1..2`, not `0..2`. Off is the separate `0x00` effect.
- Wave must set exactly one direction flag. On this firmware byte 19 drives a left-to-right sweep and byte 18 a right-to-left sweep (verified by user-observable direction).
- Static and breathing read the per-zone colours. Wave and smooth use firmware colours.

## Evidence

The layout matches the protocol used by:

- `4JX/L5P-Keyboard-RGB` / `4JX/lenovo-legion-hid`
- `kavehtehrani/legion-pro-keyboard-backlight`
- `Elahirad/LegionCenter`
- OpenRGB-derived implementations

Static 4-zone colours on this machine were previously reported as working with `HIDIOCSFEATURE`; the physical result must still be confirmed for each new effect.

## Native vs software effects

Native firmware effects:

| UI name | Byte 2 |
| --- | --- |
| Off | `0x00` |
| Static | `0x01` |
| Breathing | `0x03` |
| Wave | `0x04` |
| Smooth / hue cycle | `0x06` |

Software effects are synthesized by repeatedly writing static frames:

- Heartbeat
- Rainbow (a seamless 4-zone slice of the hue wheel that shifts continuously)
- Dance / disco
- Random
- Reactive (a keypress flashes the zone that contains the key; preset or random colour)

`legion-lightd` animates software effects at ~25 FPS and stops on write failure. The GUI animates them directly only when the daemon is not running.

## Reactive input

Reactive lighting reads the internal keyboard event device (evdev) without grabbing it, maps each key code to one of the four zones, and fades the zone colour out over roughly 0.4 s. The zone map for this machine is:

| Zone | Keys (anchors) |
| --- | --- |
| 0 (left) | F1–F5, `1`–`4`, Tab/Q/W/E/R, Caps/A/S/D/F, Shift/Z/X/C/V |
| 1 | F6–F10, `5`–`9`, T/Y/U/I/O, G/H/J/K, B/N/M/`,` |
| 2 | F11–F12, `0`/`-`/`=`/Backspace, P/`[`/`]`/`\`, L/`;`/`'`/Enter, `.`/`/`, RShift, arrows |
| 3 | numeric keypad (and the shared Insert/Home/End/PgUp/PgDn keys) |

Reading requires the `71-legion-keyboard-input.rules` uaccess rule or membership in the `input` group.

## Vantage profile slots

The EC exposes hardware lighting slots cycled by `Fn+Space`. No documented HID command selects or programs a specific Vantage slot; only the counter can be observed. The application therefore stores four equivalent userspace profiles and never claims to edit Vantage's slots.

## Safety

- Validate speed, brightness and effect before writing.
- Do not flood the controller; software effects are rate-limited.
- Treat a successful ioctl as `write reported`; physical confirmation is separate.
- The udev rule grants the active desktop user access to the vendor hidraw node. Remove it to fall back to explicit Polkit authorization.
