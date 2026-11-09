#!/usr/bin/env python3
"""Prepare the native engine when Omarchy enables this git-managed plugin."""
import fcntl
import hashlib
import json
import os
import signal
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
sys.dont_write_bytecode = True
from i18n import translate as tr
import native_build


def run(*command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=180)
    if result.returncode:
        raise RuntimeError((result.stderr.strip() or result.stdout.strip())[-2400:])
    return result.stdout


def quote(value):
    value = str(value)
    if "\n" in value or "\r" in value:
        raise ValueError(tr("Ruta de instalación no válida."))
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace("%", "%%") + '"'


def replace_if_changed(path, content, mode=None):
    """Atomically write text content; return whether anything changed."""
    if path.is_file() and path.read_text() == content and (mode is None or path.stat().st_mode & 0o777 == mode):
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(content)
    if mode is not None:
        temporary.chmod(mode)
    temporary.replace(path)
    return True


def ensure(source):
    source = Path(source).resolve()
    if not (source / "manifest.json").is_file():
        raise ValueError(tr("No se encontró el plugin."))
    for executable in ("systemctl",):
        if not shutil.which(executable):
            raise RuntimeError(tr("Falta %1. Dependencias: %2", executable, "systemd"))
    tools = native_build.discover()
    versions = native_build.versions(tools)
    state = Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state")) / "omarchy-trcc"
    config = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    state.mkdir(parents=True, exist_ok=True)
    with (state / ".bootstrap.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        # A shell restart can terminate bootstrap during compilation. Only
        # the lock owner can build, so any pre-existing build folder is stale.
        for stale in state.glob("build-*"):
            if stale.is_dir() and not stale.is_symlink():
                shutil.rmtree(stale)
        digest = hashlib.sha256(b"direct-cxx-v1\n" + versions.encode())
        for path in [source / "native_build.py", source / "i18n/messages.json", *sorted((source / "src").glob("*"))]:
            if path.is_file():
                digest.update(path.name.encode())
                digest.update(path.read_bytes())
        fingerprint = digest.hexdigest()
        marker = state / ".engine-version"
        binary = state / "bin/trofeo-engine"
        changed = not binary.is_file() or not marker.is_file() or marker.read_text() != fingerprint
        if changed:
            with tempfile.TemporaryDirectory(prefix="build-", dir=state) as temporary:
                compiled = native_build.build(source, temporary, tools=tools)
                binary.parent.mkdir(exist_ok=True)
                replacement = binary.with_suffix(".new")
                shutil.copy2(compiled, replacement)
                replacement.replace(binary)
                marker.write_text(fingerprint)
        unit = config / "systemd/user/omarchy-trcc.service"
        unit.parent.mkdir(parents=True, exist_ok=True)
        content = "\n".join([
            "[Unit]", "Description=Omarchy TRCC — native Thermalright Trofeo USB engine",
            "After=graphical-session.target", "PartOf=graphical-session.target", "", "[Service]",
            "Type=simple", "ExecStart=" + quote(binary),
            "Environment=" + quote("OMARCHY_TRCC_PLUGIN_DIR=" + str(source)),
            *["Environment=" + quote(key + "=" + os.environ[key])
              for key in ("LANG", "LC_ALL", "LC_MESSAGES", "LC_TIME", "LC_NUMERIC") if os.environ.get(key)],
            "Environment=QT_QPA_PLATFORM=offscreen", "Restart=on-failure", "RestartSec=3",
            "UMask=0077", "NoNewPrivileges=true", "", "[Install]", "WantedBy=graphical-session.target", "",
        ])
        unit_changed = replace_if_changed(unit, content)
        # The resume watcher restarts the engine after suspend. It runs from a
        # stable copy so plugin updates never leave the unit pointing nowhere.
        watcher = state / "bin/omarchy-trcc-resume-watch.sh"
        script = (source / "bin/omarchy-trcc-resume-watch.sh").read_text()
        watcher_changed = replace_if_changed(watcher, script, mode=0o755)
        resume = unit.with_name("omarchy-trcc-resume.service")
        resume_content = "".join(
            "ExecStart=" + quote(watcher) + "\n" if line.startswith("ExecStart=") else line
            for line in (source / "omarchy-trcc-resume.service").read_text().splitlines(keepends=True)
            if not line.startswith("#"))
        resume_changed = replace_if_changed(resume, resume_content)
        if unit_changed or changed or resume_changed:
            run("systemctl", "--user", "daemon-reload")
        run("systemctl", "--user", "enable", "--now", "omarchy-trcc.service")
        if changed or unit_changed:
            run("systemctl", "--user", "restart", "omarchy-trcc.service")
        run("systemctl", "--user", "enable", "--now", "omarchy-trcc-resume.service")
        if watcher_changed or resume_changed:
            run("systemctl", "--user", "restart", "omarchy-trcc-resume.service")
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
                raise RuntimeError(tr("El motor no abrió su conexión. Revisa: journalctl --user -u omarchy-trcc"))
            time.sleep(0.1)
        return {"ok": True}


if __name__ == "__main__":
    def terminate(signum, frame):
        # Unwind TemporaryDirectory on ordinary shutdown instead of leaving
        # compiler output behind when Quickshell reloads the widget.
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGTERM, terminate)
    try:
        print(json.dumps(ensure(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).parent), ensure_ascii=False))
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print(json.dumps({"ok": False, "message": str(error)}, ensure_ascii=False))
        sys.exit(1)
