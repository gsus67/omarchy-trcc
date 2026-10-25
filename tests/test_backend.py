import importlib.machinery
import importlib.util
from pathlib import Path
import json
import socket
import unittest
from unittest.mock import MagicMock, patch

loader = importlib.machinery.SourceFileLoader("trofeo", str(Path(__file__).parents[1] / "bin/trofeo"))
spec = importlib.util.spec_from_loader(loader.name, loader)
trofeo = importlib.util.module_from_spec(spec)
loader.exec_module(trofeo)


class SocketClientTests(unittest.TestCase):
    def setUp(self):
        language = patch.dict("os.environ", {"LC_ALL": "en_US.UTF-8"})
        language.start()
        self.addCleanup(language.stop)

    @patch.object(trofeo.socket, "socket")
    def test_fragmented_json_and_literal_arguments(self, factory):
        client = MagicMock()
        factory.return_value.__enter__.return_value = client
        client.recv.side_effect = [b'{"ok":', b'true,"message":"listo"}\n']
        self.assertTrue(trofeo.request("theme", ["$(id);literal"], "/tmp/unit-test.sock")["ok"])
        payload = json.loads(client.sendall.call_args.args[0])
        self.assertEqual(["$(id);literal"], payload["args"])

    @patch.object(trofeo.socket, "socket")
    def test_stopped_engine_reports_recovery(self, factory):
        factory.return_value.__enter__.return_value.connect.side_effect = FileNotFoundError()
        with self.assertRaisesRegex(ValueError, "systemctl --user start omarchy-trcc"):
            trofeo.request("status", socket_path="/tmp/unit-test.sock")

    @patch.object(trofeo.socket, "socket")
    def test_eof_is_an_error(self, factory):
        factory.return_value.__enter__.return_value.recv.return_value = b""
        with self.assertRaisesRegex(ValueError, "without a response"):
            trofeo.request("status", socket_path="/tmp/unit-test.sock")

    @patch.object(trofeo.socket, "socket")
    def test_response_size_is_bounded(self, factory):
        factory.return_value.__enter__.return_value.recv.return_value = b"x" * 65536
        with self.assertRaisesRegex(ValueError, "too large"):
            trofeo.request("status", socket_path="/tmp/unit-test.sock")


if __name__ == "__main__":
    unittest.main()
