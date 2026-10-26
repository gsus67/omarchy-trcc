#!/usr/bin/env python3
"""Build with Omarchy's C++/Qt tools; no CMake or Python packages required."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
from i18n import translate as tr

QT_MODULES = ("Qt6Core", "Qt6Gui", "Qt6Network", "libusb-1.0")
BUILD_FLAGS = ("-std=c++17", "-O2", "-fPIC", "-Wall", "-Wextra", "-Wpedantic")


def run(*command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=180)
    if result.returncode:
        raise RuntimeError((result.stderr.strip() or result.stdout.strip()
                            or tr("Falló la orden: %1", command[0]))[-2400:])
    return result.stdout


def discover():
    compiler = next((path for name in ("g++", "clang++", "c++")
                     if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError(tr("No se encontró un compilador C++ (g++ o clang++)."))
    pkg_config = shutil.which("pkg-config")
    if pkg_config is None:
        raise RuntimeError(tr("No se encontró pkg-config."))
    run(pkg_config, "--print-errors", "--exists", *QT_MODULES)
    run(pkg_config, "--atleast-version=6.7", "Qt6Core")
    # Arch's qt6-base includes rcc in this directory; do not accidentally
    # select a Qt 5 rcc that happens to come first on PATH.
    libexec = run(pkg_config, "--variable=libexecdir", "Qt6Core").strip()
    candidates = [Path(libexec) / "rcc"] if libexec else []
    candidates += [Path("/usr/lib/qt6/rcc"), Path("/usr/lib/qt6/libexec/rcc")]
    rcc = next((str(path) for path in candidates if path.is_file() and os.access(path, os.X_OK)), None)
    if rcc is None:
        rcc = shutil.which("rcc6")
    if rcc is None:
        raise RuntimeError(tr("No se encontró el generador de recursos rcc de Qt 6."))
    return {"compiler": compiler, "pkg_config": pkg_config, "rcc": rcc}


def versions(tools):
    return run(tools["pkg_config"], "--modversion", *QT_MODULES)


def write_resources(source, output):
    document = ET.Element("RCC")
    resource = ET.SubElement(document, "qresource", prefix="/")
    entry = ET.SubElement(resource, "file", alias="i18n/messages.json")
    entry.text = str((Path(source) / "i18n/messages.json").resolve())
    ET.ElementTree(document).write(output, encoding="utf-8", xml_declaration=True)


def build(source, output, *, tools=None, tests=False):
    source, output = Path(source).resolve(), Path(output).resolve()
    tools = tools or discover()
    output.mkdir(parents=True, exist_ok=True)
    qrc = output / "translations.qrc"
    resources = output / "translations.cpp"
    write_resources(source, qrc)
    run(tools["rcc"], "--name", "translations", str(qrc), "-o", str(resources))
    # pkg-config quotes/escapes paths; parse those flags without invoking a shell.
    flags = shlex.split(run(tools["pkg_config"], "--cflags", "--libs", *QT_MODULES))
    common = [tools["compiler"], *BUILD_FLAGS, "-I", str(source / "src")]
    binary = output / "trofeo-engine"
    run(*common, str(source / "src/engine.cpp"), str(source / "src/themes.cpp"),
        str(resources), *flags, "-o", str(binary))
    if tests:
        theme_test = output / "themes-test"
        run(*common, str(source / "tests/themes_test.cpp"), str(source / "src/themes.cpp"),
            str(resources), *flags, "-o", str(theme_test))
        fake_nvml = output / "libfake-nvml.so"
        run(*common, "-shared", str(source / "tests/fake_nvml.cpp"), "-o", str(fake_nvml))
        os.environ["TROFEO_TEST_NVML"] = str(fake_nvml)
        print(run(str(binary), "--self-test"), end="")
        print(run(str(theme_test)), end="")
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Keep binaries in this directory")
    parser.add_argument("--test", action="store_true", help="Build and run the native checks without USB access")
    args = parser.parse_args()
    try:
        source = Path(__file__).resolve().parent
        if args.output:
            print(build(source, args.output, tests=args.test))
        else:
            with tempfile.TemporaryDirectory(prefix="omarchy-trcc-build-") as directory:
                build(source, directory, tests=args.test)
            print(tr("Compilación terminada; archivos temporales eliminados."))
        return 0
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(json.dumps({"ok": False, "message": str(error)}, ensure_ascii=False), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
