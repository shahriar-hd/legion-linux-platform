# Architecture & Design Decisions

## 1. Read-only telemetry first
- **Decision:** Implement `core/` domain types, `ISysfsReader` / `IProcessRunner` abstractions, Linux/NVIDIA/Lenovo telemetry providers, and `legion-cli` before any privileged write path.
- **Rationale:** Establishes a testable, rootless baseline with explicit units, timestamps and capability states.
- **Safety:** No shell concatenation (`fork` + `execvp` with argument arrays and bounded timeouts), no EC writes, no kernel modules.

## 2. RGB writes through udev `uaccess`, not a root daemon
- **Decision:** Grant the active desktop user access to the ITE 8295 hidraw node with a udev `uaccess` rule. The GUI and the user-level `legion-lightd` open `/dev/hidraw*` directly.
- **Rationale:** Avoids a long-running root service, avoids a Polkit prompt on every colour change, and keeps the privileged surface small.
- **Fallback:** `legion-helper` can still write RGB through `pkexec` if the udev rule is absent. `legion-helper` only accepts validated effects and ranges.
- **Trade-off:** A local user can write the keyboard lighting. This is the same class of access as other desktop `uaccess` devices and does not grant arbitrary hardware access.

## 3. Userspace profiles instead of Vantage slots
- **Decision:** Store four lighting profiles in `~/.config/legion-linux-platform/lighting.conf`.
- **Rationale:** The documented ITE protocol accepts an effect and parameters, not a Vantage slot number. No reliable command exists to read or program the EC's Vantage slots. Claiming otherwise would be false.
- **Consequence:** The UI offers the same conceptual four profiles and effects, but they are app-managed.

## 4. Native effects plus bounded software effects
- **Decision:** Use the firmware for Off/Static/Breathing/Wave/Smooth and synthesize Heartbeat/Rainbow/Dance/Random/Reactive as rate-limited static frames.
- **Rationale:** The controller exposes only five effect IDs. The extra presets are software animations; they must not be presented as firmware modes.
- **Safety:** Software animation stops on write failure and runs at a bounded frame rate.

## 5. Passwordless Polkit only for the allowlisted helper
- **Decision:** A Polkit rule returns `YES` for `/usr/local/bin/legion-helper` when the subject is in `sudo`, `wheel` or `admin`.
- **Rationale:** The user explicitly requested no repeated password prompts for narrow controls.
- **Boundary:** The helper validates a fixed key/value allowlist for sysfs writes and a validated HID report. It cannot execute arbitrary commands or paths. Removing the rule restores password prompts.

## 6. User-level login service
- **Decision:** `legion-lightd` runs as a systemd **user** service (`WantedBy=default.target`), reads the user config and applies it at login.
- **Suspend/resume:** The daemon compares `CLOCK_BOOTTIME` with `CLOCK_MONOTONIC`; a jump means a suspend/resume occurred and the profile is re-applied.
- **Reload:** A `$XDG_RUNTIME_DIR/legion-lightd.sock` socket accepts `reload`, `apply`, `ping` and `stop`. The GUI asks the daemon to reload after saving.
- **Why not system service:** The config is per-user and RGB access is granted through the user's `uaccess` ACL. A system service would need root and a separate privileged config path.

## 7. CPU power is optional
- **Decision:** Try `/sys/class/powercap/intel-rapl*/energy_uj`; if unreadable (commonly root-only), omit CPU power and use frequency in the middle gauge ring.
- **Rationale:** Do not require root for dashboard telemetry and do not fabricate power values.

## 8. Dashboard gauges and refresh interval
- **Decision:** Replace small single-value gauges with large concentric multi-arc gauges, one per component, and let the user choose a 0.1–60 s refresh interval.
- **Rationale:** Matches the requested Vantage/Afterburner-style presentation while keeping metric semantics explicit (colour + label + unit per ring).
- **Scheduling:** The refresh timer is single-shot and rescheduled after collection, so a slow `nvidia-smi` call cannot pile up.
