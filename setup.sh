#!/usr/bin/env bash
# One-time setup, run by you with sudo. It only installs root-owned copies of
# files from this plugin; nothing it installs runs code from the plugin folder.
#
#  1. USB: seat access to the supported display only (0416:5302, uaccess).
#     No world-writable rule.
#  2. CPU power (if the CPU has RAPL): a sandboxed system service that
#     publishes whole watts once per second. The energy counter itself stays
#     root-only. See bin/power-relay.sh and omarchy-trcc-power.service.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

rule='SUBSYSTEM=="usb", ATTR{idVendor}=="0416", ATTR{idProduct}=="5302", TAG+="uaccess"'
temporary=$(mktemp)
trap 'rm -f "$temporary"' EXIT
printf '%s\n' "$rule" > "$temporary"
sudo install -o root -g root -m 644 "$temporary" /etc/udev/rules.d/70-omarchy-trcc.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=0416 --attr-match=idProduct=5302
echo 'Permiso de sesión para la Trofeo configurado. Reconéctala si el motor aún indica falta de permiso.'

if [[ -e /sys/class/powercap/intel-rapl:0/energy_uj ]]; then
  sudo install -D -o root -g root -m 755 "$here/bin/power-relay.sh" /usr/local/libexec/omarchy-trcc-power
  sudo install -o root -g root -m 644 "$here/omarchy-trcc-power.service" /etc/systemd/system/omarchy-trcc-power.service
  sudo systemctl daemon-reload
  sudo systemctl enable --now omarchy-trcc-power.service
  sudo systemctl restart omarchy-trcc-power.service
  echo 'Consumo de la CPU activado: el servicio publica vatios enteros, el contador sigue siendo solo de root.'
else
  echo 'Esta CPU no expone RAPL: el consumo de la CPU no está disponible.'
fi
