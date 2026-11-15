import contextlib
import io
import json
import os
import tempfile
import unittest
from collections import deque
from unittest.mock import patch

import codex_ha_bridge as bridge


class HomeAssistant:
    def __init__(self):
        self.helpers = {}
        self.tap = None

    def connect(self, url, **kwargs):
        assert url == "ws://localhost:8123/api/websocket"
        return Connection(self)

    def value(self, name):
        return self.helpers["input_text.codex_" + name]["state"]

    def add_helper(self, name, maximum):
        entity = "input_text." + name
        self.helpers[entity] = {"entity_id": entity, "state": "",
                                "attributes": {"min": 0, "max": maximum}}


class Connection:
    def __init__(self, ha):
        self.ha = ha
        self.inbox = deque([{"type": "auth_required"}])
        self.subscription = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        pass

    def recv(self, timeout):
        if not self.inbox:
            raise TimeoutError
        return json.dumps(self.inbox.popleft())

    def send(self, raw):
        msg = json.loads(raw)
        kind = msg["type"]
        result = None
        if kind == "auth":
            assert msg["access_token"] == "test-token"
            self.inbox.append({"type": "auth_ok"})
            return
        if kind == "get_states":
            result = list(self.ha.helpers.values())
        elif kind == "input_text/create":
            self.ha.add_helper(msg["name"], msg["max"])
        elif kind == "subscribe_events":
            assert msg["event_type"] == "esphome.codex_command"
            self.subscription = msg["id"]
        elif kind == "call_service":
            assert (msg["domain"], msg["service"]) == ("input_text", "set_value")
            entity = msg["target"]["entity_id"]
            self.ha.helpers[entity]["state"] = msg["service_data"]["value"]
            if (entity == "input_text.codex_status" and self.ha.value("status") == "Pending Approval"
                    and self.ha.tap):
                assert self.subscription is not None
                request = self.ha.value("request")
                assert request
                for action, request_id in (("reject", "stale"), ("invalid", request),
                                           (self.ha.tap, request)):
                    self.inbox.append({"id": self.subscription, "type": "event", "event": {
                        "event_type": "esphome.codex_command",
                        "data": {"action": action, "request_id": request_id},
                    }})
        else:
            raise AssertionError(f"Unexpected request {msg}")
        self.inbox.append({"id": msg["id"], "type": "result", "success": True, "result": result})


class BridgeTest(unittest.TestCase):
    def setUp(self):
        self.ha = HomeAssistant()
        cache = self.enterContext(tempfile.TemporaryDirectory())
        self.enterContext(patch.dict(os.environ, HA_URL="http://localhost:8123",
                                     HA_TOKEN="test-token", XDG_CACHE_HOME=cache))
        self.enterContext(patch("websockets.sync.client.connect", self.ha.connect))
        for name, limit in {"status": 100, "detail": 255, "request": 64, "steps": 255}.items():
            self.ha.add_helper("codex_" + name, limit)

    def invoke(self, *args, payload=None, expected=0):
        stdout = io.StringIO()
        with patch("sys.stdin", io.StringIO(json.dumps(payload))), contextlib.redirect_stdout(stdout):
            self.assertEqual(bridge.main(list(args)), expected)
        return stdout.getvalue()

    def hook(self, name, session="session-a", turn="turn-a", touch=False, **fields):
        args = ["hook", "--touch-approvals"] if touch else ["hook"]
        return json.loads(self.invoke(*args, payload={
            "hook_event_name": name, "session_id": session, "turn_id": turn,
            "cwd": "/projects/Waveshare349", **fields,
        }))

    def test_start_updates_all_four_display_helpers(self):
        self.ha.helpers["input_text.codex_request"]["state"] = "old-request"
        self.ha.helpers["input_text.codex_steps"]["state"] = "1/2|Old plan|Old next"

        self.hook("UserPromptSubmit", prompt="Private prompt")

        self.assertEqual([self.ha.value(key) for key in ("status", "detail", "steps", "request")],
                         ["Working", "Waveshare349 | Turn in progress", "", ""])

    def test_plan_and_completion_follow_the_latest_turn(self):
        self.hook("UserPromptSubmit")
        self.hook("PostToolUse", tool_name="update_plan", tool_input={"plan": [
            {"step": "Inspect", "status": "completed"},
            {"step": "Build | bridge", "status": "in_progress"},
            {"step": "Verify", "status": "pending"},
        ]})
        self.assertEqual(self.ha.value("steps"), "2/3|Build bridge|Verify")
        self.hook("UserPromptSubmit", turn="turn-b")
        self.hook("Stop", last_assistant_message="Old reply")
        self.assertEqual(self.ha.value("status"), "Working")
        self.hook("Stop", turn="turn-b", last_assistant_message="Private reply")
        self.assertEqual((self.ha.value("status"), self.ha.value("detail")),
                         ("Done", "Waveshare349 | Turn complete"))
        self.hook("SessionEnd")
        self.assertEqual(self.ha.value("status"), "")

    def test_touch_approval_and_rejection_return_codex_hook_decisions(self):
        self.hook("UserPromptSubmit")
        for action, behavior in (("approve", "allow"), ("reject", "deny")):
            with self.subTest(action=action):
                self.ha.tap = action
                result = self.hook("PermissionRequest", touch=True, tool_name="Bash",
                                   tool_input={"command": "echo hello"})
                self.assertEqual(result["hookSpecificOutput"]["decision"]["behavior"], behavior)
                self.assertEqual((self.ha.value("status"), self.ha.value("request")), ("Working", ""))

    def test_timeout_and_passive_mode_leave_the_decision_to_codex(self):
        self.hook("UserPromptSubmit")
        for touch in (False, True):
            with self.subTest(touch=touch):
                self.assertEqual(self.hook("PermissionRequest", touch=touch, tool_name="Bash",
                                           tool_input={"command": "echo hello"}), {})
                self.assertEqual((self.ha.value("status"), self.ha.value("request")), ("Needs Input", ""))

    def test_setup_creates_only_missing_helpers_and_check_reports_invalid_limits(self):
        del self.ha.helpers["input_text.codex_steps"]
        self.assertIn("MISSING input_text.codex_steps", self.invoke("check", expected=1))
        self.assertIn("OK input_text.codex_steps", self.invoke("setup"))
        self.assertIn("OK input_text.codex_steps", self.invoke("setup"))
        self.ha.helpers["input_text.codex_steps"]["attributes"]["max"] = 100
        self.assertIn("INVALID input_text.codex_steps", self.invoke("check", expected=1))

    def test_uninitialized_home_assistant_helpers_are_valid(self):
        for helper in self.ha.helpers.values():
            helper["state"] = "unknown"
        self.assertIn("OK input_text.codex_status", self.invoke("check"))

    def test_test_command_accepts_a_tap_without_running_a_codex_hook(self):
        self.ha.tap = "reject"
        self.assertIn("Display answered: reject", self.invoke("test"))
        self.assertEqual((self.ha.value("status"), self.ha.value("request")), ("Done", ""))

    def test_touch_mode_keeps_long_or_non_shell_requests_in_codex(self):
        self.hook("UserPromptSubmit")
        self.ha.tap = "approve"
        for tool, command in (("Bash", "x" * 100), ("Bash", "echo one\necho two"),
                              ("apply_patch", "some patch")):
            with self.subTest(tool=tool, command=command):
                self.assertEqual(self.hook("PermissionRequest", touch=True, tool_name=tool,
                                           tool_input={"command": command}), {})
                self.assertEqual((self.ha.value("status"), self.ha.value("request")), ("Needs Input", ""))
