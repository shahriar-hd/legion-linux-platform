#!/usr/bin/env bash
set -euo pipefail

systemctl --user disable --now legion-lightd.service 2>/dev/null || true
rm -f "${HOME}/.config/systemd/user/legion-lightd.service"
systemctl --user daemon-reload 2>/dev/null || true

if [[ "${EUID}" -eq 0 ]]; then
    rm -f /usr/local/bin/legion-helper /usr/local/bin/legion-lightd
    rm -f /etc/udev/rules.d/60-legion-rgb.rules
    rm -f /etc/polkit-1/rules.d/50-legion-helper.rules
    rm -rf /usr/local/share/doc/legion-linux-platform
    udevadm control --reload-rules || true
    udevadm trigger --subsystem-match=hidraw || true
    echo "Removed system files."
else
    echo "Removed the user service. Run with sudo to remove system files."
fi
