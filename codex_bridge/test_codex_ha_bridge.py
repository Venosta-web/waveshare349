import argparse
import contextlib
import io
import json
import os
import tempfile
import time
import unittest
from collections import deque
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from unittest.mock import patch

import codex_ha_bridge as bridge


class HomeAssistant:
    def __init__(self):
        self.helpers = {}
        self.tap = None
        self.failed_values = deque()

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
            value = msg["service_data"]["value"]
            if self.ha.failed_values and value == self.ha.failed_values[0]:
                self.ha.failed_values.popleft()
                raise TimeoutError
            self.ha.helpers[entity]["state"] = value
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

    def transcript(self):
        return Path(self.enterContext(tempfile.TemporaryDirectory())) / "turn.jsonl"

    def append_record(self, path, kind="event_msg", **payload):
        with path.open("a") as stream:
            stream.write(json.dumps({"type": kind, "payload": payload}) + "\n")

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

    def test_usage_limit_without_stop_shows_only_the_retry_time(self):
        self.hook("UserPromptSubmit")
        transcript = self.transcript()
        self.append_record(transcript, type="task_started", turn_id="turn-a")
        self.append_record(transcript, type="task_complete", turn_id="turn-a", error={
            "codex_error_info": "usage_limit_exceeded",
            "message": "Private account detail. Try again at Nov 16th, 2026 4:30 PM.",
        })
        self.invoke("observe", "session-a", "turn-a", str(transcript), "Waveshare349")
        self.hook("Stop")
        self.assertEqual((self.ha.value("status"), self.ha.value("detail")),
                         ("Rate Limited", "Waveshare349 | Try again at Nov 16, 2026 4:30 PM"))

    def test_only_an_unambiguous_same_turn_reset_supplies_a_countdown(self):
        reset = int(time.time()) + 1800
        for secondary_percent, has_countdown in ((45, True), (100, False)):
            with self.subTest(secondary_percent=secondary_percent):
                self.hook("UserPromptSubmit")
                transcript = self.transcript()
                self.append_record(transcript, type="task_started", turn_id="turn-a")
                self.append_record(transcript, type="token_count", rate_limits={
                    "primary": {"used_percent": 100, "resets_at": reset},
                    "secondary": {"used_percent": secondary_percent, "resets_at": reset + 1000},
                })
                self.append_record(transcript, type="task_complete", turn_id="turn-a", error={
                    "codex_error_info": "usage_limit_exceeded",
                })
                self.invoke("observe", "session-a", "turn-a", str(transcript), "Waveshare349")
                self.assertEqual(self.ha.value("status"), "Rate Limited")
                if has_countdown:
                    prefix, epoch, seconds, clock, detail = self.ha.value("detail").split("|", 4)
                    self.assertEqual((prefix, int(epoch), detail), ("reset", reset, "Waveshare349 | Usage limit reached"))
                    self.assertTrue(1790 <= int(seconds) <= 1800)
                    self.assertTrue(clock)
                else:
                    self.assertEqual(self.ha.value("detail"), "Waveshare349 | Usage limit reached")

    def test_retries_stay_working_and_late_transcript_updates_leave_the_new_chat_in_control(self):
        self.hook("UserPromptSubmit")
        transcript = self.transcript()
        self.append_record(transcript, type="task_started", turn_id="turn-a")
        self.append_record(transcript, type="error", will_retry=True, message="Private error")
        with ThreadPoolExecutor(max_workers=1) as runner:
            observer = runner.submit(bridge.main, ["observe", "session-a", "turn-a", str(transcript), "Waveshare349"])
            try:
                deadline = time.monotonic() + 2
                while self.ha.value("detail") != "Waveshare349 | Retrying" and time.monotonic() < deadline:
                    time.sleep(0.01)
                self.assertEqual((self.ha.value("status"), self.ha.value("detail")),
                                 ("Working", "Waveshare349 | Retrying"))
            finally:
                self.hook("UserPromptSubmit", session="session-b", turn="turn-b", cwd="/projects/Other")
                self.append_record(transcript, type="task_complete", turn_id="turn-a", error={
                    "codex_error_info": "usage_limit_exceeded",
                })
            self.assertEqual(observer.result(timeout=2), 0)
        self.assertEqual((self.ha.value("status"), self.ha.value("detail")),
                         ("Working", "Other | Turn in progress"))

    def test_other_terminal_errors_have_a_distinct_state_and_keep_error_text_local(self):
        self.hook("UserPromptSubmit")
        transcript = self.transcript()
        self.append_record(transcript, type="task_complete", turn_id="turn-a", error={
            "codex_error_info": "response_stream_disconnected", "message": "Private network details",
        })
        self.invoke("observe", "session-a", "turn-a", str(transcript), "Waveshare349")
        self.assertEqual((self.ha.value("status"), self.ha.value("detail")),
                         ("Turn Failed", "Waveshare349 | See Codex"))

    def test_stop_waits_for_transcript_and_observer_retries_after_ha_timeout(self):
        transcript = self.transcript()
        # Stand in for the separately launched process while running its CLI below.
        with patch("subprocess.Popen", return_value=argparse.Namespace(pid=os.getpid())):
            self.hook("UserPromptSubmit", transcript_path=str(transcript))
        self.hook("Stop")
        self.assertEqual(self.ha.value("status"), "Working")
        self.append_record(transcript, type="task_complete", turn_id="turn-a", error={
            "codex_error_info": "rate_limit_exceeded",
        })
        self.ha.failed_values.append("Rate Limited")
        self.invoke("observe", "session-a", "turn-a", str(transcript), "Waveshare349")
        self.assertEqual((self.ha.value("status"), self.ha.value("detail")),
                         ("Rate Limited", "Waveshare349 | Request rate limit reached"))

    def test_running_tests_preserves_plan_and_keeps_command_arguments_local(self):
        self.hook("UserPromptSubmit")
        self.hook("PostToolUse", tool_name="update_plan", tool_input={"plan": [
            {"step": "Verify bridge", "status": "in_progress"},
            {"step": "Review", "status": "pending"},
        ]})
        self.hook("PreToolUse", tool_name="Bash", tool_input={"command": "python3 -m pytest private-path"})
        self.assertEqual((self.ha.value("status"), self.ha.value("detail"), self.ha.value("steps")),
                         ("Working", "Waveshare349 | Running tests", "1/2|Verify bridge|Review"))

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
