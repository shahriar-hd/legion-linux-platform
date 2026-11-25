#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -eq 0 ]]; then
    echo "Run this as your desktop user, not with sudo." >&2
    exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVICE_DIR="${HOME}/.config/systemd/user"

install -D -m 0644 "${ROOT_DIR}/packaging/systemd/legion-lightd.service" \
    "${SERVICE_DIR}/legion-lightd.service"

systemctl --user daemon-reload
systemctl --user enable --now legion-lightd.service
echo "Enabled legion-lightd.service for ${USER}."
echo "Check it with: systemctl --user status legion-lightd.service"
