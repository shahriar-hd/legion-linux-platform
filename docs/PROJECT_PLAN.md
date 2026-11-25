# Project Plan and Acceptance Criteria

## MVP recommendation

Do not start with overclocking, raw EC access, or kernel driver development. The MVP should prove reliable read-only monitoring and a stable architecture first.

### Milestone 0 — Discovery report
- Inventory machine, BIOS, kernel, CPU, GPUs, storage, keyboard controller.
- Test existing LenovoLegionLinux and OpenRGB compatibility.
- Record existing sysfs/hwmon/powercap/platform_profile interfaces.
- Produce support matrix: detected/readable/writable/verified/unsupported.
- No writes, no stress testing.

Acceptance: user approves the target distro and first-release scope.

### Milestone 1 — Read-only CLI
- CPU, GPU, RAM, disk, battery, fan, network inventory.
- JSON output with source, units, timestamp, state.
- No root requirement for ordinary telemetry where possible.

Acceptance: values cross-checked against `sensors`, `nvidia-smi`, `smartctl`, `nvme`, `free`, and OS tools.

### Milestone 2 — Daemon + GUI
- D-Bus service, Polkit, Qt/QML dashboard.
- SQLite sensor history and export.
- Capability states and error visibility.

Acceptance: no GUI root execution; daemon can be absent without crashing the GUI.

### Milestone 3 — Safe Lenovo controls
- Platform profile and validated controls exposed by existing drivers.
- Fan curve only after exact-model write verification.
- Battery charge limit if exposed.

Acceptance: physical behavior verified and firmware-auto restore tested.

### Milestone 4 — Keyboard lighting
- Identify exact keyboard controller and transport.
- Test static zone color, then effects.
- Persist a profile and reapply on login/boot only with opt-in.

Acceptance: user sees physical four-zone changes after reboot and resume; no false success.

### Milestone 5 — Diagnostics
- CPU/GPU/RAM/storage/network tests.
- Test preflight, warnings, live telemetry, abort and report.
- Safe storage defaults.

Acceptance: tests have bounded duration and successful abort behavior.

### Milestone 6 — Cross-vendor plugin architecture
- Define backend interface and manifest.
- Add one additional vendor only after target Legion implementation is stable.

Acceptance: common GUI and core do not contain vendor-specific conditionals.

## Release blockers

- Any control that can disable thermal safety.
- Any unverified EC write.
- GUI running as root.
- No recovery path for manual fan control.
- Stress test without timeout/abort.
- Destructive storage test enabled by default.
- Claims of RGB/fan/overclock support without exact-model evidence.

## Status — 2026-11-25

- **Milestone 0 (discovery):** complete for the `82JF` / `H1CN58WW` target.
- **Milestone 1 (read-only CLI):** complete; CPU usage, storage and GPU clock added.
- **Milestone 2 (GUI):** multi-arc dashboard, controls, user-set refresh interval, capability tables.
- **Milestone 3 (Lenovo controls):** platform profile and VPC toggles via the allowlisted helper; passwordless Polkit rule packaged.
- **Milestone 4 (keyboard lighting):** native Off/Static/Breathing/Wave/Smooth, software Heartbeat/Rainbow/Dance/Random/Reactive, four profiles, painted keyboard preview, login/suspend restore daemon. Physical verification of the new effects is pending.
- **Milestone 5 (diagnostics):** not started.
- **Milestone 6 (multi-vendor):** not started.

### Next recommended steps

1. Install the udev/Polkit rules and the user service, then confirm passwordless controls and login restore.
2. Physically verify each native and software effect on the `048d:c965` keyboard.
3. Add a read-only SMART/NVMe health screen and a bounded diagnostics catalogue.
