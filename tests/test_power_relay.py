"""CPU power relay: bin/power-relay.sh, its service unit and setup.sh.

The relay runs as root, so these tests guard both what it publishes (whole
watts, never the raw counter) and how it is installed and sandboxed.
"""
import os
import re
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RELAY = ROOT / "bin/power-relay.sh"
UNIT = ROOT / "omarchy-trcc-power.service"
SETUP = ROOT / "setup.sh"
UNINSTALL = ROOT / "uninstall.sh"


class RelayTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.rapl = Path(self.temp.name, "rapl")
        self.out = Path(self.temp.name, "out")
        self.rapl.mkdir()
        self.out.mkdir()
        (self.rapl / "max_energy_range_uj").write_text("1000000000\n")
        self.process = None

    def tearDown(self):
        if self.process and self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        self.temp.cleanup()

    def start(self):
        env = dict(os.environ, OMARCHY_TRCC_RAPL_DIR=str(self.rapl), OMARCHY_TRCC_POWER_DIR=str(self.out))
        self.process = subprocess.Popen(["bash", str(RELAY)], env=env)

    def wait_for_value(self, timeout=4.0):
        target = self.out / "cpu-watts"
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if target.exists():
                return target.read_text()
            time.sleep(0.05)
        self.fail("relay published nothing")

    def feed(self, start, watts, wraps=False):
        """Advance the fake counter by `watts` µJ per µs, in real time, and
        return the relay's second value: on a busy runner this feeder can
        stall while the relay starts, skewing only its first interval."""
        target = self.out / "cpu-watts"
        (self.rapl / "energy_uj").write_text(f"{start}\n")
        self.start()
        began = time.monotonic()
        published = []
        while len(published) < 2 and time.monotonic() - began < 8:
            time.sleep(0.005)
            value = start + int((time.monotonic() - began) * 1e6 * watts)
            if wraps:
                value %= 1000000000
            (self.rapl / "energy_uj").write_text(f"{value}\n")
            if target.exists():
                stamp = target.stat().st_mtime_ns
                if not published or published[-1] != stamp:
                    published.append(stamp)
        if len(published) < 2:
            self.fail("relay published fewer than two values")
        return target.read_text()

    def test_publishes_whole_watts_only(self):
        text = self.feed(5_000_000, 42)
        self.assertRegex(text, r"^\d+\n$", "only a whole number of watts")
        self.assertLessEqual(abs(int(text) - 42), 2)
        self.assertEqual(sorted(p.name for p in self.out.iterdir()), ["cpu-watts"], "no other files")
        self.assertEqual((self.out / "cpu-watts").stat().st_mode & 0o777, 0o644)

    def test_counter_wrap(self):
        # 30 W reaches the 1e9 µJ range at ~1.5 s: inside the checked second interval.
        text = self.feed(955_000_000, 30, wraps=True)
        self.assertLessEqual(abs(int(text) - 30), 2)

    def test_without_rapl_exits_quietly(self):
        result = subprocess.run(["bash", str(RELAY)], timeout=5, env=dict(
            os.environ, OMARCHY_TRCC_RAPL_DIR=str(self.rapl / "missing"), OMARCHY_TRCC_POWER_DIR=str(self.out)))
        self.assertEqual(result.returncode, 0)
        self.assertEqual(list(self.out.iterdir()), [])


class InstallSecurityTest(unittest.TestCase):
    def unit(self):
        values = {}
        for line in UNIT.read_text().splitlines():
            if "=" in line and not line.lstrip().startswith(("#", "[")):
                key, value = line.split("=", 1)
                values.setdefault(key.strip(), []).append(value.strip())
        return values

    def test_unit_is_sandboxed(self):
        unit = self.unit()
        required = {
            "CapabilityBoundingSet": "", "AmbientCapabilities": "", "NoNewPrivileges": "yes",
            "ProtectSystem": "strict", "ProtectHome": "yes", "PrivateNetwork": "yes",
            "RestrictAddressFamilies": "none", "IPAddressDeny": "any", "PrivateDevices": "yes",
            "ProtectKernelTunables": "yes", "ProtectKernelModules": "yes", "RestrictNamespaces": "yes",
            "RestrictSUIDSGID": "yes", "MemoryDenyWriteExecute": "yes", "LockPersonality": "yes",
            "SystemCallArchitectures": "native",
        }
        for key, value in required.items():
            with self.subTest(key=key):
                self.assertIn(value, unit.get(key, []), f"{key}={value} is required")
        self.assertNotIn("User", unit, "runs as root only to read the root-owned counter")
        self.assertIn("@system-service", unit["SystemCallFilter"])

    def test_unit_never_runs_plugin_files(self):
        exec_start = self.unit()["ExecStart"]
        self.assertEqual(exec_start, ["/usr/local/libexec/omarchy-trcc-power"])

    def test_setup_installs_root_owned_copies(self):
        text = SETUP.read_text()
        self.assertRegex(text, r"install -D -o root -g root -m 755 \"\$here/bin/power-relay.sh\" /usr/local/libexec/omarchy-trcc-power")
        self.assertRegex(text, r"install -o root -g root -m 644 \"\$here/omarchy-trcc-power.service\" /etc/systemd/system/")
        self.assertNotRegex(text, r"chmod|chown|0?666|0?777|a\+r|o\+r", "never widens permissions")
        self.assertNotIn("energy_uj ", re.sub(r"\[\[.*?\]\]", "", text), "never touches the counter itself")

    def test_uninstall_removes_the_service(self):
        text = UNINSTALL.read_text()
        for path in ("/etc/systemd/system/omarchy-trcc-power.service", "/usr/local/libexec/omarchy-trcc-power"):
            self.assertIn(path, text)
        self.assertIn("disable --now omarchy-trcc-power.service", text)


if __name__ == "__main__":
    unittest.main()
