#!/usr/bin/env python3
"""Prepare the native engine when Omarchy enables this git-managed plugin."""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time


def run(*command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=180)
    if result.returncode:
        raise RuntimeError((result.stderr.strip() or result.stdout.strip())[-2400:])
    return result.stdout


def quote(value):
    value = str(value)
    if "\n" in value or "\r" in value:
        raise ValueError("Ruta de instalación no válida.")
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace("%", "%%") + '"'


def ensure(source):
    source = Path(source).resolve()
    if not (source / "manifest.json").is_file():
        raise ValueError("No se encontró el plugin.")
    for executable in ("cmake", "g++", "pkg-config", "systemctl"):
        if not shutil.which(executable):
            raise RuntimeError("Falta " + executable + ". Dependencias: omarchy pkg add cmake gcc pkgconf qt6-base libusb python libpulse")
    versions = run("pkg-config", "--modversion", "Qt6Core", "libusb-1.0")
    state = Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state")) / "omarchy-trcc"
    config = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    state.mkdir(parents=True, exist_ok=True)
    with (state / ".bootstrap.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        digest = hashlib.sha256(versions.encode())
        for path in [source / "CMakeLists.txt", *sorted((source / "src").glob("*"))]:
            if path.is_file():
                digest.update(path.name.encode())
                digest.update(path.read_bytes())
        fingerprint = digest.hexdigest()
        marker = state / ".engine-version"
        binary = state / "bin/trofeo-engine"
        changed = not binary.is_file() or not marker.is_file() or marker.read_text() != fingerprint
        if changed:
            with tempfile.TemporaryDirectory(prefix="build-", dir=state) as temporary:
                run("cmake", "-S", str(source), "-B", temporary, "-DCMAKE_BUILD_TYPE=Release")
                run("cmake", "--build", temporary, "--target", "trofeo-engine", "-j2")
                binary.parent.mkdir(exist_ok=True)
                replacement = binary.with_suffix(".new")
                shutil.copy2(Path(temporary) / "trofeo-engine", replacement)
                replacement.replace(binary)
                marker.write_text(fingerprint)
        unit = config / "systemd/user/omarchy-trcc.service"
        unit.parent.mkdir(parents=True, exist_ok=True)
        content = "\n".join([
            "[Unit]", "Description=Omarchy TRCC — native Thermalright Trofeo USB engine",
            "After=graphical-session.target", "PartOf=graphical-session.target", "", "[Service]",
            "Type=simple", "ExecStart=" + quote(binary),
            "Environment=" + quote("OMARCHY_TRCC_PLUGIN_DIR=" + str(source)),
            "Environment=QT_QPA_PLATFORM=offscreen", "Restart=on-failure", "RestartSec=3",
            "UMask=0077", "NoNewPrivileges=true", "", "[Install]", "WantedBy=graphical-session.target", "",
        ])
        unit_changed = not unit.exists() or unit.read_text() != content
        if unit_changed:
            temporary = unit.with_suffix(".tmp")
            temporary.write_text(content)
            temporary.replace(unit)
        if unit_changed or changed:
            run("systemctl", "--user", "daemon-reload")
        run("systemctl", "--user", "enable", "--now", "omarchy-trcc.service")
        if changed or unit_changed:
            run("systemctl", "--user", "restart", "omarchy-trcc.service")
        # systemd reports a Type=simple service active before Qt has opened
        # its socket. Don't tell the panel it's ready until it really replies.
        runtime = Path(os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"))
        deadline = time.monotonic() + 15
        while True:
            try:
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                    client.settimeout(3)
                    client.connect(str(runtime / "omarchy-trcc.sock"))
                    client.sendall(b'{"command":"status"}\n')
                    if client.recv(64).startswith(b'{'):
                        break
            except OSError:
                pass
            if time.monotonic() >= deadline:
                raise RuntimeError("El motor no abrió su conexión. Revisa: journalctl --user -u omarchy-trcc")
            time.sleep(0.1)
        return {"ok": True}


if __name__ == "__main__":
    try:
        print(json.dumps(ensure(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).parent), ensure_ascii=False))
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print(json.dumps({"ok": False, "message": str(error)}, ensure_ascii=False))
        sys.exit(1)
