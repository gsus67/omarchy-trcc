#!/usr/bin/env bash
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
config_root="${XDG_CONFIG_HOME:-$HOME/.config}"
target="$config_root/omarchy/plugins/omarchy-trcc"
service_dir="$config_root/systemd/user"
command -v omarchy >/dev/null
command -v python3 >/dev/null
cmake -S "$source_dir" -B "$source_dir/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$source_dir/build" -j2
ctest --test-dir "$source_dir/build" --output-on-failure
omarchy plugin validate "$source_dir"
systemctl --user stop omarchy-trcc.service 2>/dev/null || true
mkdir -p "$target/bin" "$target/assets" "$service_dir"
install -m 644 "$source_dir/manifest.json" "$source_dir/Trofeo.qml" "$source_dir/uninstall.sh" "$target/"
install -m 644 "$source_dir/assets/trofeo.svg" "$target/assets/"
install -m 755 "$source_dir/bin/trofeo" "$source_dir/build/trofeo-engine" "$target/bin/"
cat > "$service_dir/omarchy-trcc.service" <<'UNIT'
[Unit]
Description=Omarchy TRCC — native Thermalright Trofeo USB engine
After=graphical-session.target
PartOf=graphical-session.target

[Service]
Type=simple
ExecStart="%E/omarchy/plugins/omarchy-trcc/bin/trofeo-engine"
Restart=on-failure
RestartSec=3
Environment=QT_QPA_PLATFORM=offscreen
UMask=0077
NoNewPrivileges=true

[Install]
WantedBy=graphical-session.target
UNIT
systemctl --user daemon-reload
systemctl --user enable omarchy-trcc.service
systemctl --user restart omarchy-trcc.service
omarchy-shell shell rescanPlugins
omarchy plugin enable omarchy-trcc --section right
echo 'Omarchy TRCC v0.0.1 instalado: icono de pantalla con onda en la barra.'
