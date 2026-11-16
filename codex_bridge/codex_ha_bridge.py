#!/usr/bin/env python3
"""Connect local Codex lifecycle hooks to the Waveshare display through Home Assistant."""

import argparse
from collections import deque
from contextlib import contextmanager
from datetime import datetime, timezone
import fcntl
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from urllib.parse import urlsplit
import uuid

from websockets.exceptions import WebSocketException


HELPERS = {"request": 64, "steps": 255, "detail": 255, "status": 100}
EVENT_TYPE = "esphome.codex_command"
IO_TIMEOUT = 2
APPROVAL_TIMEOUT = 45
SHELL_REVIEW_LIMIT = 42
OBSERVE_INTERVAL = 0.25
STOP_GRACE = 3
DELIVERY_RETRY_TIMEOUT = 30
TRANSCRIPT_START_TIMEOUT = 10
TRANSCRIPT_IDLE_TIMEOUT = 3600
BRIDGE_ERRORS = (OSError, ValueError, RuntimeError, WebSocketException)


def entity(field):
    return f"input_text.codex_{field}"


def text(value, limit):
    return " ".join(value.split()).encode("ascii", "replace").decode()[:limit]


def config():
    config_home = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config")
    values = {}
    for name in ("claude-ha-bridge", "codex-ha-bridge"):
        try:
            lines = (config_home / name / "config.env").read_text().splitlines()
        except FileNotFoundError:
            continue
        for line in lines:
            key, sep, value = line.partition("=")
            if sep and key.strip() in {"HA_URL", "HA_TOKEN"}:
                values[key.strip()] = value.strip().strip('"').strip("'")

    url = urlsplit(os.environ.get("HA_URL") or values.get("HA_URL") or
                   "http://homeassistant.local:8123")
    token = os.environ.get("HA_TOKEN") or values.get("HA_TOKEN")
    if not token:
        raise ValueError("Set HA_TOKEN in the environment or codex-ha-bridge/config.env")
    if (url.scheme not in {"http", "https"} or not url.hostname or url.username
            or url.password or url.query or url.fragment):
        raise ValueError("HA_URL must be an HTTP(S) base URL without credentials")
    scheme = "wss" if url.scheme == "https" else "ws"
    return f"{scheme}://{url.netloc}{url.path.rstrip('/')}/api/websocket", token


class HomeAssistant:
    def __init__(self, socket, token):
        self.socket = socket
        self.next_id = 0
        self.events = deque(maxlen=64)
        if self.receive().get("type") != "auth_required":
            raise RuntimeError("Unexpected Home Assistant WebSocket greeting")
        socket.send(json.dumps({"type": "auth", "access_token": token}))
        if self.receive().get("type") != "auth_ok":
            raise RuntimeError("Home Assistant rejected the token")

    @classmethod
    @contextmanager
    def connect(cls):
        from websockets.sync.client import connect

        url, token = config()
        with connect(url, open_timeout=IO_TIMEOUT, close_timeout=IO_TIMEOUT,
                     max_size=16 * 1024 * 1024, proxy=None) as socket:
            yield cls(socket, token)

    def receive(self, timeout=IO_TIMEOUT):
        return json.loads(self.socket.recv(timeout=timeout))

    def call(self, kind, **fields):
        self.next_id += 1
        self.socket.send(json.dumps({"id": self.next_id, "type": kind, **fields}))
        deadline = time.monotonic() + IO_TIMEOUT
        while (remaining := deadline - time.monotonic()) > 0:
            message = self.receive(remaining)
            if message.get("type") == "event":
                self.events.append(message)
            elif message.get("id") == self.next_id:
                if not message.get("success"):
                    error = message.get("error", {})
                    raise RuntimeError(f"{kind} failed: {error.get('message', 'unknown error')}")
                return message.get("result")
        raise TimeoutError(f"{kind} timed out")

    def publish(self, status, detail="", steps="", request=""):
        fields = {"request": request, "steps": steps, "detail": detail, "status": status}
        order = ("steps", "detail", "request", "status") if request else HELPERS
        for field in order:
            value = fields[field]
            self.call("call_service", domain="input_text", service="set_value",
                      target={"entity_id": entity(field)}, service_data={"value": value})

    def subscribe(self):
        self.call("subscribe_events", event_type=EVENT_TYPE)
        return self.next_id

    def decision(self, subscription, request_id):
        deadline = time.monotonic() + APPROVAL_TIMEOUT
        while (remaining := deadline - time.monotonic()) > 0:
            try:
                message = self.events.popleft() if self.events else self.receive(remaining)
            except TimeoutError:
                break
            event = message.get("event", {})
            if (message.get("type") != "event" or message.get("id") != subscription
                    or event.get("event_type") != EVENT_TYPE):
                continue
            data = event.get("data", {})
            if data.get("request_id") == request_id and data.get("action") in ("approve", "reject"):
                return data["action"]
        return None


@contextmanager
def display_state():
    """Serialize ownership changes and writes across independent hook processes."""
    cache = Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache") / "codex-ha-bridge"
    cache.mkdir(mode=0o700, parents=True, exist_ok=True)
    path = cache / "display.json"
    with (cache / "display.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            state = json.loads(path.read_text())
        except (FileNotFoundError, ValueError):
            state = {}
        previous = state.copy()
        try:
            yield state
        finally:
            if state != previous:
                temp = path.with_suffix(".tmp")
                temp.write_text(json.dumps(state))
                temp.replace(path)


def format_plan(plan):
    if not isinstance(plan, list) or not plan:
        return ""
    if any(not isinstance(item, dict) or not isinstance(item.get("step"), str)
           or item.get("status") not in ("pending", "in_progress", "completed") for item in plan):
        return ""
    current = next((i for i, item in enumerate(plan) if item["status"] == "in_progress"), None)
    if current is None:
        current = next((i for i, item in enumerate(plan) if item["status"] == "pending"), None)
    if current is None:
        return ""
    remaining = (item["step"] for item in plan[current + 1:] if item["status"] != "completed")
    label = text(plan[current]["step"].replace("|", " "), 112)
    next_label = text(next(remaining, "").replace("|", " "), 112)
    return f"{current + 1}/{len(plan)}|{label}|{next_label}" if label else ""


def publish(state, status, detail="", steps="", request="", ha=None):
    state.update(status=status, detail=detail, steps=steps, request=request)
    if ha is not None:
        ha.publish(status, detail, steps, request)
    else:
        with HomeAssistant.connect() as client:
            client.publish(status, detail, steps, request)


RETRY_TIME = re.compile(
    r"\btry again at (?:(?P<month>Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) "
    r"(?P<day>[0-9]{1,2})(?:st|nd|rd|th)?, (?P<year>[0-9]{4}) )?"
    r"(?P<clock>(?:[01]?[0-9]|2[0-3]):[0-5][0-9](?: [AP]M)?)\b", re.IGNORECASE,
)
TOOL_ACTIVITY = {
    "apply_patch": "Editing", "web_search": "Searching the web", "web__run": "Searching the web",
    "spawn_agent": "Delegating work", "update_plan": "Updating plan",
}
SHELL_TOOLS = {"Bash", "exec_command"}
TEST_COMMAND = re.compile(r"^(?:(?:\S*/)?(?:python[0-9.]*\s+-m\s+)?(?:pytest|unittest)\b|"
                          r"(?:npm|pnpm|yarn|bun|cargo|go|uv|make)\s+(?:run\s+)?test\b|"
                          r"uv\s+run\s+pytest\b)")
READ_COMMAND = re.compile(r"^(?:cat|head|tail|sed|rg|grep|ls|find|nl|stat|readlink)\b|"
                          r"^git\s+(?:diff|show|status|log|ls-files)\b")
CODE_SHELL_CALL = re.compile(r'\btools\.exec_command\(\s*\{\s*cmd\s*:\s*("(?:[^"\\]|\\.)*")')


def tool_activity(tool, inputs):
    if not isinstance(tool, str):
        return None
    if tool in SHELL_TOOLS:
        command = inputs.get("command", inputs.get("cmd"))
        if isinstance(command, list) and command and all(isinstance(arg, str) for arg in command):
            if (len(command) >= 3 and Path(command[0]).name in ("sh", "bash", "zsh", "fish")
                    and command[1] in ("-c", "-lc")):
                command = command[2]
            else:
                command = " ".join(command)
        if isinstance(command, str):
            command = command.lstrip()
            if TEST_COMMAND.match(command):
                return "Running tests"
            if READ_COMMAND.match(command):
                return "Reading files"
        return "Running command"
    return TOOL_ACTIVITY.get(tool)


def transcript_activity(kind, record):
    event = record.get("type")
    if kind == "response_item" and event in ("function_call", "custom_tool_call"):
        tool = record.get("name")
        if tool == "exec" and isinstance(script := record.get("input"), str):
            if "tools.apply_patch(" in script:
                return "Editing"
            if match := CODE_SHELL_CALL.search(script):
                try:
                    return tool_activity("exec_command", {"cmd": json.loads(match[1])})
                except ValueError:
                    pass
            if "tools.web__run(" in script:
                return "Searching the web"
            return "Using tools"
        inputs = record.get("arguments")
        if isinstance(tool, str) and tool in SHELL_TOOLS and isinstance(inputs, str):
            try:
                inputs = json.loads(inputs)
            except ValueError:
                inputs = None
        return tool_activity(tool, inputs if isinstance(inputs, dict) else {}) or "Using tools"
    if kind == "response_item" and event == "reasoning":
        return "Thinking"
    if kind == "response_item" and event == "message" and record.get("role") == "assistant":
        return "Writing update" if record.get("phase") == "commentary" else "Writing response"
    if kind == "event_msg":
        if event == "error" and record.get("will_retry", record.get("willRetry")) is True:
            return "Retrying"
        item = record.get("item")
        if event in ("item_started", "item_completed") and isinstance(item, dict):
            item_type = item.get("type")
            if not isinstance(item_type, str):
                return None
            if item_type == "CommandExecution":
                label = tool_activity("exec_command", item)
                parsed = item.get("parsed_cmd")
                if label == "Running command" and isinstance(parsed, list) and parsed and all(
                    isinstance(action, dict) and action.get("type") in ("read", "search", "list_files")
                    for action in parsed
                ):
                    return "Reading files"
                return label
            return {"FileChange": "Editing", "ImageView": "Inspecting image", "McpToolCall": "Using tools",
                    "SubAgentActivity": "Delegating work", "Reasoning": "Thinking"}.get(item_type)
    return None


def quota_reset(error, limits):
    reset = error.get("resets_at")
    if type(reset) is int and 0 < reset <= 253402214399:
        return reset
    exhausted = [window.get("resets_at") for key in ("primary", "secondary")
                 if isinstance(window := limits.get(key), dict) and window.get("used_percent") == 100]
    if exhausted and all(type(value) is int and 0 < value <= 253402214399 and value == exhausted[0]
                         for value in exhausted):
        return exhausted[0]
    return None


def completion(error, project, limits):
    if not error:
        return "Done", f"{project} | Turn complete"
    error_kind = error.get("codex_error_info") if isinstance(error, dict) else None
    if error_kind not in ("usage_limit_exceeded", "rate_limit_exceeded"):
        return "Turn Failed", f"{project} | See Codex"

    message = error.get("message")
    retry = RETRY_TIME.search(message) if isinstance(message, str) else None
    clock = ""
    if retry:
        clock = retry["clock"].upper()
        if retry["month"]:
            clock = f"{retry['month'].title()} {retry['day']}, {retry['year']} {clock}"
    reset = quota_reset(error, limits if error_kind == "usage_limit_exceeded" else {})
    if reset is not None:
        clock = datetime.fromtimestamp(reset, timezone.utc).astimezone().strftime("%H:%M %Z").strip()
    reason = "Usage limit reached" if error_kind == "usage_limit_exceeded" else "Request rate limit reached"
    summary = f"Try again at {clock}" if clock else reason
    detail = f"{project} | {summary}"
    if reset is not None:
        detail = f"reset|{reset}|{max(0, reset - int(time.time()))}|{clock}|{project} | {reason}"
    return "Rate Limited", detail


class TurnTranscript:
    def __init__(self, path, turn):
        self.path = Path(path)
        self.turn = turn
        self.offset = 0
        self.active = False
        self.limits = {}

    def records(self):
        with self.path.open("rb") as stream:
            stream.seek(self.offset)
            for line in stream:
                if not line.endswith(b"\n"):
                    break
                self.offset += len(line)
                try:
                    entry = json.loads(line)
                except (ValueError, UnicodeError):
                    continue
                if not isinstance(entry, dict) or not isinstance(record := entry.get("payload"), dict):
                    continue
                kind = entry.get("type")
                event = record.get("type")
                record_turn = record.get("turn_id")
                if kind == "turn_context" or (kind == "event_msg" and event == "task_started"):
                    self.active = record_turn == self.turn
                if record_turn == self.turn or (record_turn is None and self.active):
                    yield kind, record

    def read(self, project):
        activity = None
        progressed = False
        try:
            for kind, record in self.records():
                progressed = True
                event = record.get("type")
                if kind == "event_msg" and event == "token_count":
                    snapshot = record.get("rate_limits")
                    self.limits = snapshot if isinstance(snapshot, dict) else {}
                if kind == "event_msg" and record.get("turn_id") == self.turn:
                    if event == "task_complete":
                        return progressed, None, completion(record.get("error"), project, self.limits)
                    if event == "turn_aborted":
                        return progressed, None, ("", f"{project} | Turn interrupted")
                activity = transcript_activity(kind, record) or activity
        except FileNotFoundError:
            pass
        return progressed, activity, None


def deliver(state, status, detail, steps):
    next_state = state.copy()
    try:
        publish(next_state, status, detail, steps)
    except BRIDGE_ERRORS:
        return False
    state.update(next_state)
    return True


def owns_display(state, session, turn):
    return (state.get("session"), state.get("turn")) == (session, turn)


def observe_turn(session, turn, path, project):
    transcript = TurnTranscript(path, turn)
    idle_until = time.monotonic() + TRANSCRIPT_START_TIMEOUT
    outcome = None
    activity = None
    pid = os.getpid()
    try:
        while time.monotonic() < idle_until:
            with display_state() as state:
                if not owns_display(state, session, turn) or (state.get("terminal") and outcome is None):
                    return
                if state.get("observer_pid", pid) != pid:
                    return

            if outcome is None:
                progressed, latest_activity, outcome = transcript.read(project)
                if progressed:
                    idle_until = time.monotonic() + TRANSCRIPT_IDLE_TIMEOUT
                activity = latest_activity or activity

            with display_state() as state:
                if not owns_display(state, session, turn) or (state.get("terminal") and outcome is None):
                    return
                if state.get("observer_pid", pid) != pid:
                    return
                if outcome:
                    if not state.get("terminal"):
                        idle_until = time.monotonic() + DELIVERY_RETRY_TIMEOUT
                    state["terminal"] = True
                    steps = state.get("steps", "") if outcome[0] == "Done" else ""
                    if deliver(state, *outcome, steps):
                        return
                elif time.monotonic() >= state.get("stop_deadline", float("inf")):
                    return
                elif activity and state.get("status") == "Working":
                    detail = f"{project} | {activity}"
                    if state.get("detail") == detail or deliver(state, "Working", detail, state.get("steps", "")):
                        activity = None
                else:
                    activity = None
            time.sleep(OBSERVE_INTERVAL)
    finally:
        with display_state() as state:
            if owns_display(state, session, turn) and state.get("observer_pid", pid) == pid:
                state.pop("observer_pid", None)
                if state.pop("stop_deadline", None) is not None and not state.get("terminal"):
                    state["terminal"] = True
                    publish(state, "Done", f"{project} | Turn complete", state.get("steps", ""))


def start_observer(session, turn, path, project):
    return subprocess.Popen(
        [sys.executable, str(Path(__file__).resolve()), "observe", "--", session, turn, path, project],
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        start_new_session=True,
    ).pid


def observer_is_running(state):
    pid = state.get("observer_pid")
    if not isinstance(pid, int) or pid <= 0:
        return False
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def ask_display(session, turn, detail, fallback, test=False):
    request = uuid.uuid4().hex
    action = None
    with HomeAssistant.connect() as ha:
        subscription = ha.subscribe()
        try:
            with display_state() as state:
                if test:
                    if state.get("status") in ("Working", "Pending Approval", "Needs Input"):
                        raise RuntimeError("Wait for the current Codex turn to finish")
                elif not owns_display(state, session, turn) or state.get("terminal"):
                    return None
                if state.get("request"):
                    return None
                if test:
                    state.update(session=session, turn=turn)
                publish(state, "Pending Approval", detail, state.get("steps", ""), request, ha)
            action = ha.decision(subscription, request)
        finally:
            with display_state() as state:
                if state.get("request") != request:
                    action = None
                elif test:
                    publish(state, "Done", f"Bridge test | {action or 'No tap received'}", ha=ha)
                else:
                    status = "Working" if action else "Needs Input"
                    outcome = {"approve": "Tool approved", "reject": "Tool rejected"}
                    project = detail.split(" | ", 1)[0]
                    message = f"{project} | {outcome[action]}" if action else fallback
                    publish(state, status, message, state.get("steps", ""), ha=ha)
    return action


def run_hook(payload, touch_approvals):
    if not isinstance(payload, dict):
        raise ValueError("Expected a Codex hook object")
    event = payload.get("hook_event_name")
    session = payload.get("session_id")
    turn = payload.get("turn_id")
    if not isinstance(session, str) or not session:
        return {}
    cwd = payload.get("cwd")
    project = text(Path(cwd).name.replace("|", " "), 48) if isinstance(cwd, str) else ""
    project = project or "Codex"
    tool = payload.get("tool_name")
    raw_input = payload.get("tool_input")
    tool_input = raw_input if isinstance(raw_input, dict) else {}
    fallback = f"{project} | Review the request in Codex"
    command = tool_input.get("command")
    can_show_command = (tool == "Bash" and isinstance(command, str)
                        and 0 < len(command) <= SHELL_REVIEW_LIMIT
                        and command.isascii() and command.isprintable())
    if event == "PermissionRequest" and touch_approvals and can_show_command:
        action = ask_display(session, turn, f"{project} | Bash: {command}", fallback)
        if action:
            decision = {"behavior": "allow"} if action == "approve" else {
                "behavior": "deny", "message": "Rejected on the Waveshare display",
            }
            return {"hookSpecificOutput": {"hookEventName": event, "decision": decision}}
        return {}

    with display_state() as state:
        if event == "UserPromptSubmit":
            state.clear()
            state.update(session=session, turn=turn)
            path = payload.get("transcript_path")
            try:
                if isinstance(turn, str) and turn and isinstance(path, str) and path and Path(path).is_absolute():
                    state["observer_pid"] = start_observer(session, turn, path, project)
            finally:
                publish(state, "Working", f"{project} | Turn in progress")
            return {}
        if state.get("session") != session or (event != "SessionEnd" and state.get("turn") != turn):
            return {}

        steps = state.get("steps", "")
        if event == "SessionEnd":
            state.clear()
            publish(state, "")
        elif state.get("terminal"):
            return {}
        elif event == "Stop":
            if observer_is_running(state):
                state.setdefault("stop_deadline", time.monotonic() + STOP_GRACE)
            else:
                state["terminal"] = True
                publish(state, "Done", f"{project} | Turn complete", steps)
        elif event == "Interrupt":
            state["terminal"] = True
            publish(state, "", f"{project} | Turn interrupted")
        elif not state.get("request"):
            if event == "PermissionRequest":
                publish(state, "Needs Input", fallback, steps)
            elif event == "PreToolUse" and tool == "request_user_input":
                publish(state, "Needs Input", f"{project} | Answer in Codex", steps)
            elif event == "PreToolUse" and state.get("status") == "Working":
                if activity := tool_activity(tool, tool_input):
                    detail = f"{project} | {activity}"
                    if state.get("detail") != detail:
                        publish(state, "Working", detail, steps)
            elif event == "PostToolUse" and tool == "update_plan":
                detail = state.get("detail") if state.get("status") == "Working" else None
                publish(state, "Working", detail or f"{project} | Turn in progress",
                        format_plan(tool_input.get("plan")))
            elif event == "PostToolUse" and state.get("status") == "Needs Input":
                publish(state, "Working", f"{project} | Turn in progress", steps)
    return {}


def helper_states(ha):
    return {entry["entity_id"]: entry for entry in ha.call("get_states")
            if entry["entity_id"].startswith("input_text.codex_")}


def check_helpers(create):
    with HomeAssistant.connect() as ha:
        helpers = helper_states(ha)
        if create:
            for field, maximum in HELPERS.items():
                if entity(field) not in helpers:
                    ha.call("input_text/create", name=f"codex_{field}", min=0, max=maximum, mode="text")
            helpers = helper_states(ha)

    valid = True
    for field, maximum in HELPERS.items():
        name = entity(field)
        helper = helpers.get(name)
        if helper is None:
            print(f"MISSING {name}")
            valid = False
        else:
            attrs = helper["attributes"]
            if helper["state"] == "unavailable":
                print(f"INVALID {name} (unavailable)")
                valid = False
            elif attrs.get("min") != 0 or attrs.get("max", 0) < maximum:
                print(f"INVALID {name} (requires min 0 and max at least {maximum})")
                valid = False
            else:
                print(f"OK {name}")
    return 0 if valid else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    hook = commands.add_parser("hook", help="Read a Codex lifecycle event on stdin")
    hook.add_argument("--touch-approvals", action="store_true")
    observe = commands.add_parser("observe", help="Follow the displayed turn's local transcript")
    for field in ("session", "turn", "transcript", "project"):
        observe.add_argument(field)
    commands.add_parser("setup", help="Create missing Home Assistant helpers and verify them")
    commands.add_parser("check", help="Verify Home Assistant helper IDs and lengths")
    commands.add_parser("test", help="Wait for a touchscreen tap without involving Codex")
    args = parser.parse_args(argv)
    if args.command == "hook":
        try:
            result = run_hook(json.load(sys.stdin), args.touch_approvals)
        except BRIDGE_ERRORS as error:
            print(f"Codex HA bridge: {error}", file=sys.stderr)
            result = {}
        print(json.dumps(result))
        return 0
    try:
        if args.command == "observe":
            observe_turn(args.session, args.turn, args.transcript, args.project)
            return 0
        if args.command == "test":
            print(f"Tap APPROVE or REJECT on the Codex display within {APPROVAL_TIMEOUT}s.", flush=True)
            action = ask_display("bridge-test", uuid.uuid4().hex,
                                 "Bridge test | Test request", "", test=True)
            print(f"Display answered: {action or 'no tap received'}")
            return 0 if action else 1
        return check_helpers(create=args.command == "setup")
    except BRIDGE_ERRORS as error:
        print(f"Codex HA bridge: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
