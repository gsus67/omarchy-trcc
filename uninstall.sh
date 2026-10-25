#!/usr/bin/env bash
set -euo pipefail
purge=false
if [[ ${1:-} == --purge ]]; then purge=true
elif [[ $# -gt 0 ]]; then echo "Uso: bash uninstall.sh [--purge]" >&2; exit 2; fi
config_root="${XDG_CONFIG_HOME:-$HOME/.config}"
state_root="${XDG_STATE_HOME:-$HOME/.local/state}"
runtime="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
systemctl --user disable --now omarchy-trcc.service 2>/dev/null || true
omarchy plugin disable omarchy-trcc || true
if [[ -d "$config_root/omarchy/plugins/omarchy-trcc" ]]; then
  omarchy plugin remove omarchy-trcc --yes
fi
rm -f "$config_root/systemd/user/omarchy-trcc.service"
systemctl --user daemon-reload
rm -f "$runtime/omarchy-trcc.sock" "$runtime/omarchy-trcc.sock.lock"
rm -f "$state_root/omarchy-trcc/bin/trofeo-engine" "$state_root/omarchy-trcc/.engine-version" "$state_root/omarchy-trcc/.bootstrap.lock"
rmdir "$state_root/omarchy-trcc/bin" 2>/dev/null || true
if $purge; then
  rm -rf -- "$state_root/omarchy-trcc"
  if [[ -f /etc/udev/rules.d/70-omarchy-trcc.rules ]]; then
    sudo rm -f /etc/udev/rules.d/70-omarchy-trcc.rules
    sudo udevadm control --reload-rules
  fi
fi
echo 'Omarchy TRCC desinstalado.'
