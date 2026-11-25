#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
    echo "Run this installer with sudo: sudo $0" >&2
    exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HELPER="${ROOT_DIR}/build/legion-helper"
LIGHTD="${ROOT_DIR}/build/legion-lightd"

if [[ ! -x "${HELPER}" || ! -x "${LIGHTD}" ]]; then
    echo "Build the project first: cmake -B build -G Ninja && cmake --build build" >&2
    exit 1
fi

install -m 0755 "${HELPER}" /usr/local/bin/legion-helper
install -m 0755 "${LIGHTD}" /usr/local/bin/legion-lightd
install -D -m 0644 "${ROOT_DIR}/packaging/udev/60-legion-rgb.rules" /etc/udev/rules.d/60-legion-rgb.rules
install -D -m 0644 "${ROOT_DIR}/packaging/polkit/50-legion-helper.rules" /etc/polkit-1/rules.d/50-legion-helper.rules
install -D -m 0644 "${ROOT_DIR}/README.md" /usr/local/share/doc/legion-linux-platform/README.md

udevadm control --reload-rules
udevadm trigger --subsystem-match=hidraw

TARGET_USER="${SUDO_USER:-}"
if [[ -z "${TARGET_USER}" || "${TARGET_USER}" == "root" ]]; then
    echo "System files installed."
    echo "Now run this as your desktop user to install the login service:"
    echo "  ${ROOT_DIR}/packaging/install-user-service.sh"
    exit 0
fi

TARGET_HOME="$(getent passwd "${TARGET_USER}" | cut -d: -f6)"
if [[ -z "${TARGET_HOME}" ]]; then
    echo "Could not resolve home directory for ${TARGET_USER}." >&2
    exit 1
fi

TARGET_GROUP="$(id -gn "${TARGET_USER}")"
install -d -m 0755 -o "${TARGET_USER}" -g "${TARGET_GROUP}" "${TARGET_HOME}/.config/systemd/user"
install -m 0644 -o "${TARGET_USER}" -g "${TARGET_GROUP}" \
    "${ROOT_DIR}/packaging/systemd/legion-lightd.service" \
    "${TARGET_HOME}/.config/systemd/user/legion-lightd.service"

echo "System files installed and user service copied for ${TARGET_USER}."
echo "Enable it from ${TARGET_USER}'s desktop session with:"
echo "  systemctl --user daemon-reload"
echo "  systemctl --user enable --now legion-lightd.service"
