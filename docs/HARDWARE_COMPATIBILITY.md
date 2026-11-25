# Hardware Compatibility Matrix

## Target machine: Lenovo Legion 5 Pro 16ITH6 (`82JF`)
- **BIOS:** `H1CN58WW`
- **OS / kernel:** Ubuntu 26.04.1 LTS, kernel `7.0.0-34-generic`
- **CPU:** 11th Gen Intel Core i7-11800H
- **GPU:** NVIDIA GeForce RTX 3050 Ti Laptop GPU (driver `595.104.02`)
- **Keyboard:** 4-zone RGB SKU (`048d:c965`, ITE Device 8295), plus ITE `048d:c101` (8910)

| Capability | Backend | State | Evidence | Notes |
| --- | --- | --- | --- | --- |
| ACPI platform profile | `linux_sysfs` | `readable` | `/sys/firmware/acpi/platform_profile` | Choices: `low-power balanced performance custom` |
| Linux hwmon sensors | `linux_sysfs` | `readable` | `/sys/class/hwmon` (6 devices) | `coretemp`, two `nvme`, `iwlwifi_1`, `ADP0`, `BAT0` |
| CPU frequency / max frequency | `linux_sysfs` | `readable` | `cpufreq/scaling_cur_freq`, `cpuinfo_max_freq` | MHz |
| CPU utilization | `linux_sysfs` | `readable` | `/proc/stat` deltas | First sample is unavailable; later refreshes are valid |
| CPU package power | `linux_sysfs` | `temporarily_unavailable` | `intel-rapl:0/energy_uj` | Node is root-only on this install; gauge falls back to frequency |
| Storage usage | `linux_sysfs` | `readable` | `statvfs("/")` | Root filesystem total/used/percent |
| Battery telemetry | `linux_sysfs` + `lenovo_acpi` | `readable` | `/sys/class/power_supply/BAT0` | Charge, status, health, cycles, power, charge type |
| Memory telemetry | `linux_sysfs` | `readable` | `/proc/meminfo` | Total and available |
| NVIDIA GPU telemetry | `nvidia_smi` | `readable` | `nvidia-smi --query-gpu=...` | Temp, power, utilization, VRAM, graphics clock; fan is `N/A` on this GPU |
| Lenovo ACPI controls | `lenovo_acpi` + `legion-helper` | `readable` | `VPC2004:00/{conservation_mode,fan_mode,fn_lock,usb_charging,camera_power}` | Writes go through Polkit |
| 4-zone RGB detection | `lenovo_acpi` | `detected` | `/dev/hidraw1`, `/dev/hidraw2` (`048d:c965`) | Write path implemented |
| RGB native Off/Static/Breathing/Wave/Smooth | `lenovo_ite8295` | `writable` / `applied` | 33-byte `0xCC` feature report | Static 4-zone colours were physically verified previously; the other native effects still need a fresh physical confirmation |
| RGB software Heartbeat/Rainbow/Dance/Random/Reactive | `lenovo_ite8295` + `legion-lightd` | `applied` (write reported) | Static frames at a bounded rate | Physical result must be confirmed per effect |
| Login lighting restore | `legion-lightd` (systemd user service) | `applied` | `~/.config/legion-linux-platform/lighting.conf` | Re-applies at login and after suspend/resume |

## Known limitations

- Lenovo Vantage's four EC lighting slots are not programmable through the documented HID protocol. The app stores four userspace profiles instead.
- CPU package power is unavailable without read access to RAPL; no root requirement is added for it.
- GPU fan speed is reported as `N/A` by `nvidia-smi` on this RTX 3050 Ti configuration.
- Physical verification of the new native and software effects is pending user confirmation.
