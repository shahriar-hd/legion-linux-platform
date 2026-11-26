# Test Results

## 2026-11-25 — Milestone 1 build and verification
- **Build:** `cmake -B build -G Ninja && cmake --build build` — passed.
- **Unit tests:** `ctest --test-dir build --output-on-failure` — passed (mocked `ISysfsReader` / `IProcessRunner`).
- **Real-hardware read-only run:** `./build/legion-cli` — passed on `Legion 5 Pro 16ITH6 (82JF)`, BIOS `H1CN58WW`, kernel `7.0.0-34-generic`.

## 2026-11-25 — GUI, helper, and static 4-zone RGB
- **Build:** `legion-cli`, `legion-helper`, `legion-gui`, `legion_unit_tests` — passed.
- **Real-hardware static RGB:** previously reported working through `HIDIOCSFEATURE` on `048d:c965`.

## 2026-11-25 — Profiles, effects, daemon, dashboard
- **Build:** `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build` — passed with no warnings.
- **Unit tests:** `ctest --test-dir build --output-on-failure` — **2/2 passed**:
  - `legion_unit_tests` — telemetry parsing, timeout handling, VPC/RGB detection.
  - `legion_lighting_tests` — config round-trip, effect aliases, report byte layout, wave direction flags, off/smooth, invalid ranges, frame sampler determinism, HSV conversion.
- **Read-only CLI:** `./build/legion-cli` — passed. New readings observed: CPU max frequency, storage total/used/percent, NVIDIA graphics clock. NVIDIA fan reported `N/A` (`temporarily_unavailable`).
- **GUI smoke test:** `QT_QPA_PLATFORM=offscreen timeout 3 ./build/legion-gui` — started without crashing; the Qt offscreen plugin printed only `propagateSizeHints()`.
- **GUI layout screenshots:** offscreen grabs of Dashboard, Controls, 4-Zone RGB, and Sensors tabs were captured and inspected. The multi-arc gauges, tab bar, keyboard preview, zone buttons, and tables render correctly. The fan gauge reports unavailable because this kernel exposes no hwmon fan inputs.
- **Daemon one-shot:** `./build/legion-lightd --once` without a config exits cleanly and does not write hardware.

## Not yet verified

- Physical confirmation of Breathing, Wave (both directions), Smooth, Heartbeat, Rainbow, Dance, Random and Reactive on the real keyboard.
- udev `uaccess` access after install and reconnect.
- systemd user service start at login and re-apply after suspend/resume.
- Passwordless Polkit behaviour after installing the rule.
- CPU package power (RAPL is root-only on this machine).
- **Daemon dry-run:** `./build/legion-lightd --config /tmp/legion-test.conf --once --dry-run` — parsed a Rainbow profile and logged the intended apply without touching hardware.

## 2026-11-25 — Temperature units, reactive modes, keymap, logo
- **Build:** `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build` — passed with no warnings.
- **Unit tests:** `ctest --test-dir build --output-on-failure` — **2/2 passed**. `legion_lighting_tests` now also covers:
  - reactive preset/random render and decay,
  - the corrected Wave direction bytes (LTR → byte 19, RTL → byte 18),
  - the Lenovo key-to-zone map (A/F5 zone 0, O/F10 zone 1, Enter/Up zone 2, keypad zone 3, unknown → -1).
- **GUI:** offscreen run of every tab started and exited cleanly. The `logo.png` Qt resource compiled into the GUI.
- **Not verified:** physical reactive flashes (needs the keyboard-input rule), realtime colour preview on hardware, and the F/C toggle appearance.
