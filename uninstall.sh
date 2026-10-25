#!/usr/bin/env bash
set -euo pipefail
purge=false
if [[ ${1:-} == --purge ]]; then purge=true
elif [[ $# -gt 0 ]]; then echo "Uso: bash uninstall.sh [--purge]" >&2; exit 2; fi
config_root="${XDG_CONFIG_HOME:-$HOME/.config}"
state_root="${XDG_STATE_HOME:-$HOME/.local/state}"
runtime="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
systemctl --user disable --now omarchy-trcc.service 2>/dev/null || true
# The plugin id was omarchy-trcc before v0.0.6.
for id in io.github.gsus67.omarchy-trcc omarchy-trcc; do
  omarchy plugin disable "$id" 2>/dev/null || true
  if [[ -d "$config_root/omarchy/plugins/$id" ]]; then
    omarchy plugin remove "$id" --yes
  fi
done
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
