#!/usr/bin/env python3
"""Connect local Codex lifecycle hooks to the Waveshare display through Home Assistant."""

import argparse
from collections import deque
from contextlib import contextmanager
import fcntl
import json
import os
from pathlib import Path
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
    state.update(status=status, steps=steps, request=request)
    if ha is not None:
        ha.publish(status, detail, steps, request)
    else:
        with HomeAssistant.connect() as client:
            client.publish(status, detail, steps, request)


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
                elif (state.get("session"), state.get("turn")) != (session, turn):
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
            state.update(session=session, turn=turn)
            publish(state, "Working", f"{project} | Turn in progress")
            return {}
        if state.get("session") != session or (event != "SessionEnd" and state.get("turn") != turn):
            return {}

        steps = state.get("steps", "")
        if event == "SessionEnd":
            state.clear()
            publish(state, "")
        elif event == "Stop":
            publish(state, "Done", f"{project} | Turn complete", steps)
        elif event == "Interrupt":
            publish(state, "", f"{project} | Turn interrupted")
        elif not state.get("request"):
            if event == "PermissionRequest":
                publish(state, "Needs Input", fallback, steps)
            elif event == "PreToolUse" and tool == "request_user_input":
                publish(state, "Needs Input", f"{project} | Answer in Codex", steps)
            elif event == "PostToolUse" and tool == "update_plan":
                publish(state, "Working", f"{project} | Turn in progress",
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
