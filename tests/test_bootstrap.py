import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import MagicMock, patch
import bootstrap


class BootstrapCleanupTests(unittest.TestCase):
    def test_interrupted_build_cleanup_preserves_themes_and_reuses_the_binary(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "plugin"
            (source / "src").mkdir(parents=True)
            (source / "i18n").mkdir()
            (source / "manifest.json").write_text("{}")
            (source / "native_build.py").write_text("# build recipe")
            (source / "src/engine.cpp").write_text("// source")
            (source / "i18n/messages.json").write_text("{}")
            state = root / "state/omarchy-trcc"
            stale = state / "build-interrupted"
            stale.mkdir(parents=True)
            (stale / "partial.o").write_text("partial output")
            theme = state / "themes/mine.json"
            theme.parent.mkdir()
            theme.write_text("user theme")

            def build(source, output, **kwargs):
                binary = Path(output) / "trofeo-engine"
                binary.write_bytes(b"fake compiled engine")
                return binary

            client = MagicMock()
            client.__enter__.return_value.recv.return_value = b'{}\n'
            environment = {"XDG_STATE_HOME": str(root / "state"),
                           "XDG_CONFIG_HOME": str(root / "config"),
                           "XDG_RUNTIME_DIR": str(root / "runtime")}
            with patch.dict(os.environ, environment), \
                    patch.object(bootstrap.shutil, "which", return_value="/fake/systemctl"), \
                    patch.object(bootstrap.native_build, "discover", return_value={}), \
                    patch.object(bootstrap.native_build, "versions", return_value="6.12\n1.0\n"), \
                    patch.object(bootstrap.native_build, "build", side_effect=build) as compiler, \
                    patch.object(bootstrap, "run", return_value=""), \
                    patch.object(bootstrap.socket, "socket", return_value=client):
                self.assertTrue(bootstrap.ensure(source)["ok"])
                self.assertFalse(stale.exists())
                self.assertEqual(theme.read_text(), "user theme")
                self.assertFalse(list(state.glob("build-*")))
                self.assertTrue(bootstrap.ensure(source)["ok"])
                self.assertEqual(compiler.call_count, 1)
                self.assertEqual((state / "bin/trofeo-engine").read_bytes(), b"fake compiled engine")


if __name__ == "__main__":
    unittest.main()
