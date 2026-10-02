"""Optional navigation/sleep routes for the user's existing Flask/Pi TV server."""
import os
import re
import subprocess
import threading

KEYS = {"up": 19, "down": 20, "left": 21, "right": 22, "ok": 23, "off": 223}


def register_remote(app, tv_serial=None, adb="adb"):
    """Call before app.run(), reusing the server's already-authorized TV address."""
    serial = tv_serial or os.environ.get("JOYN_TV_ADB", "")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*:[0-9]{1,5}", serial):
        raise ValueError("Supply the existing Xiaomi ADB address as host:port")
    if not 1 <= int(serial.rsplit(":", 1)[1]) <= 65535:
        raise ValueError("Invalid ADB port")
    paths = {"/remote/capabilities", "/remote/key/<key>"}
    if any(rule.rule in paths for rule in app.url_map.iter_rules()):
        raise ValueError("Remote routes already registered")
    lock = threading.Lock()

    def capabilities():
        return {"version": 1, "keys": list(KEYS)}, 200

    def key_command(key):
        if key not in KEYS:
            return {"ok": False, "error": "Unsupported key"}, 400
        if not lock.acquire(blocking=False):
            return {"ok": False, "error": "TV command busy"}, 409
        try:
            state = subprocess.run([adb, "-s", serial, "get-state"], capture_output=True,
                                   text=True, timeout=2, check=False)
            if state.returncode or state.stdout.strip() != "device":
                return {"ok": False, "error": "TV offline or ADB not authorized"}, 503
            # SLEEP (223), never the POWER toggle (26). Never retry this command.
            result = subprocess.run([adb, "-s", serial, "shell", "input", "keyevent", str(KEYS[key])],
                                    capture_output=True, text=True, timeout=5, check=False)
            if result.returncode or result.stderr.strip():
                return {"ok": False, "error": "TV command failed"}, 502
            return {"ok": True, "status": "command_sent", "key": key}, 200
        except subprocess.TimeoutExpired:
            return {"ok": False, "error": "ADB timeout"}, 504
        except OSError:
            return {"ok": False, "error": "ADB unavailable"}, 503
        finally:
            lock.release()

    app.add_url_rule("/remote/capabilities", endpoint="lilygo_remote_capabilities",
                     view_func=capabilities, methods=["GET"])
    app.add_url_rule("/remote/key/<key>", endpoint="lilygo_remote_key",
                     view_func=key_command, methods=["POST"])
