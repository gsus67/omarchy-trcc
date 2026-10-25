#!/usr/bin/env bash
# Grant seat access only to the supported USB display. No world-writable rule.
set -euo pipefail
rule='SUBSYSTEM=="usb", ATTR{idVendor}=="0416", ATTR{idProduct}=="5302", TAG+="uaccess"'
temporary=$(mktemp)
trap 'rm -f "$temporary"' EXIT
printf '%s\n' "$rule" > "$temporary"
sudo install -m 644 "$temporary" /etc/udev/rules.d/70-omarchy-trcc.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=0416 --attr-match=idProduct=5302
echo 'Permiso de sesión para la Trofeo configurado. Reconéctala si el motor aún indica falta de permiso.'
