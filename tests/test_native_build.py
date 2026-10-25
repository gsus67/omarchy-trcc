import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import xml.etree.ElementTree as ET
import native_build


class NativeBuildTests(unittest.TestCase):
    def test_resource_paths_are_escaped_and_keep_the_expected_alias(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "source with spaces & accents á"
            root.mkdir()
            qrc = Path(directory) / "translations.qrc"
            native_build.write_resources(root, qrc)
            resource = ET.parse(qrc).find("./qresource/file")
            self.assertEqual(resource.attrib["alias"], "i18n/messages.json")
            self.assertEqual(resource.text, str(root / "i18n/messages.json"))

    @patch.object(native_build, "run")
    @patch.object(native_build.shutil, "which")
    @patch.object(native_build.os, "access", return_value=True)
    @patch.object(native_build.Path, "is_file", return_value=True)
    def test_clang_is_used_when_gcc_is_missing(self, is_file, access, which, run):
        which.side_effect = lambda name: {"clang++": "/usr/bin/clang++", "pkg-config": "/usr/bin/pkg-config"}.get(name)
        run.side_effect = ["", "", "/usr/lib/qt6\n"]
        tools = native_build.discover()
        self.assertEqual(tools["compiler"], "/usr/bin/clang++")


if __name__ == "__main__":
    unittest.main()
