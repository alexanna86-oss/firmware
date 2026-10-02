import importlib.util
import pathlib
import subprocess
import types
import unittest
from unittest.mock import patch

path = pathlib.Path(__file__).resolve().parents[2] / "pi" / "remote_routes.py"
spec = importlib.util.spec_from_file_location("remote_routes", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class App:
    def __init__(self):
        self.routes = {}
        self.url_map = types.SimpleNamespace(iter_rules=lambda: [types.SimpleNamespace(rule=r) for r in self.routes])

    def add_url_rule(self, path, **kwargs):
        self.routes[path] = kwargs


class RemoteRoutes(unittest.TestCase):
    def setUp(self):
        self.app = App()
        module.register_remote(self.app, "192.0.2.1:5555")
        self.command = self.app.routes["/remote/key/<key>"]["view_func"]

    def test_readonly_capabilities_and_post_only_mutation(self):
        with patch.object(module.subprocess, "run") as run:
            result, status = self.app.routes["/remote/capabilities"]["view_func"]()
            self.assertEqual(status, 200)
            self.assertIn("off", result["keys"])
            run.assert_not_called()
        self.assertEqual(self.app.routes["/remote/key/<key>"]["methods"], ["POST"])

    def test_off_is_sleep_not_toggle(self):
        with patch.object(module.subprocess, "run", side_effect=[
            subprocess.CompletedProcess([], 0, "device\n", ""),
            subprocess.CompletedProcess([], 0, "", "")]) as run:
            self.assertTrue(self.command("off")[0]["ok"])
            self.assertEqual(run.call_count, 2)
            self.assertEqual(run.call_args.args[0][-3:], ["input", "keyevent", "223"])

    def test_reject_unknown_command(self):
        with patch.object(module.subprocess, "run") as run:
            self.assertEqual(self.command("power; reboot")[1], 400)
            run.assert_not_called()

    def test_offline_not_sent(self):
        with patch.object(module.subprocess, "run", return_value=subprocess.CompletedProcess([], 1, "", "")) as run:
            self.assertEqual(self.command("off")[1], 503)
            self.assertEqual(run.call_count, 1)

    def test_timeout_releases_lock_without_retry(self):
        with patch.object(module.subprocess, "run", side_effect=subprocess.TimeoutExpired("adb", 2)) as run:
            self.assertEqual(self.command("off")[1], 504)
            self.assertEqual(self.command("off")[1], 504)
            self.assertEqual(run.call_count, 2)

    def test_duplicate_and_invalid_configuration(self):
        with self.assertRaises(ValueError):
            module.register_remote(self.app, "192.0.2.1:5555")
        for address in ["", "host;cmd:5555", "host:0", "host:65536"]:
            with patch.dict(module.os.environ, {}, clear=True), self.assertRaises(ValueError):
                module.register_remote(App(), address)
