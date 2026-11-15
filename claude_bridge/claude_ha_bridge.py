#!/usr/bin/env python3
"""Claude Code <-> Home Assistant <-> Waveshare display bridge.

Subcommands:
  hook    Claude Code hook entry point (reads hook JSON on stdin).
  setup   Create the input_text helpers in Home Assistant if missing.
  check   Verify the token and that the helpers exist.
  test    Send a fake permission request to the display and print the tap.

Config: ~/.config/claude-ha-bridge/config.env (HA_URL=..., HA_TOKEN=...).

Safety rule: the hook never denies on its own. If the display does not answer
in time, Home Assistant is unreachable, or anything fails, it prints nothing
and Claude Code falls back to its normal permission prompt.
"""
import asyncio
import json
import os
import re
import signal
import sys
import urllib.request
import uuid
from pathlib import Path

CONFIG_FILE = Path.home() / ".config" / "claude-ha-bridge" / "config.env"
EVENT_TYPE = "esphome.claude_command"
ENT_STATUS = "input_text.claude_status"
ENT_DETAIL = "input_text.claude_detail"
ENT_REQUEST = "input_text.claude_request"
HELPERS = {  # name -> max length; entity id becomes input_text.<name>
    "claude_status": 100,
    "claude_detail": 255,
    "claude_request": 64,
}
DECISION_TIMEOUT = float(os.environ.get("CLAUDE_HA_DECISION_TIMEOUT", "110"))
CONNECT_TIMEOUT = 3
# Tools whose prompt needs more than yes/no; leave them to the normal dialog.
SKIP_TOOLS = {"AskUserQuestion"}


def load_config():
    cfg = {}
    try:
        for line in CONFIG_FILE.read_text().splitlines():
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = line.split("=", 1)
                cfg[k.strip()] = v.strip().strip('"').strip("'")
    except OSError:
        pass
    url = (os.environ.get("HA_URL") or cfg.get("HA_URL") or "http://homeassistant.local:8123").rstrip("/")
    token = os.environ.get("HA_TOKEN") or cfg.get("HA_TOKEN") or ""
    return url, token


HA_URL, HA_TOKEN = load_config()
HA_WS = re.sub(r"^http", "ws", HA_URL) + "/api/websocket"


def ascii_clip(text, limit):
    """The display fonts only carry ASCII; collapse whitespace and trim."""
    text = " ".join(str(text).split())
    text = text.encode("ascii", "replace").decode("ascii")
    return text if len(text) <= limit else text[: limit - 3] + "..."


# ---------------------------------------------------------------- REST (quick, fire-and-forget)

def rest_set(entity_id, value, limit):
    req = urllib.request.Request(
        f"{HA_URL}/api/services/input_text/set_value",
        data=json.dumps({"entity_id": entity_id, "value": ascii_clip(value, limit)}).encode(),
        headers={"Authorization": f"Bearer {HA_TOKEN}", "Content-Type": "application/json"},
        method="POST",
    )
    try:
        urllib.request.urlopen(req, timeout=2).read()
        return True
    except Exception:
        return False


def set_display(status=None, detail=None, request=None):
    """Best effort; stops at the first failure so a down HA costs one timeout."""
    if not HA_TOKEN:
        return
    for ent, value, limit in ((ENT_REQUEST, request, 64), (ENT_DETAIL, detail, 255), (ENT_STATUS, status, 100)):
        if value is not None and not rest_set(ent, value, limit):
            return


def display_ready():
    """True if HA answers and the status helper exists (fast fail before waiting)."""
    req = urllib.request.Request(f"{HA_URL}/api/states/{ENT_STATUS}",
                                 headers={"Authorization": f"Bearer {HA_TOKEN}"})
    try:
        return urllib.request.urlopen(req, timeout=CONNECT_TIMEOUT).status == 200
    except Exception:
        return False


# ---------------------------------------------------------------- WebSocket client

class HAWebSocket:
    def __init__(self, ws):
        self.ws = ws
        self.next_id = 1
        self.events = []

    @classmethod
    async def open(cls):
        from websockets.asyncio.client import connect

        ws = await connect(HA_WS, open_timeout=CONNECT_TIMEOUT, max_size=2**22)
        client = cls(ws)
        msg = json.loads(await asyncio.wait_for(ws.recv(), CONNECT_TIMEOUT))
        if msg.get("type") != "auth_required":
            raise RuntimeError(f"unexpected greeting: {msg}")
        await ws.send(json.dumps({"type": "auth", "access_token": HA_TOKEN}))
        msg = json.loads(await asyncio.wait_for(ws.recv(), CONNECT_TIMEOUT))
        if msg.get("type") != "auth_ok":
            raise RuntimeError("Home Assistant rejected the token")
        return client

    async def call(self, payload, timeout=5):
        msg_id = self.next_id
        self.next_id += 1
        await self.ws.send(json.dumps({"id": msg_id, **payload}))
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while True:
            msg = json.loads(await asyncio.wait_for(self.ws.recv(), deadline - loop.time()))
            if msg.get("type") == "event":
                self.events.append(msg)
            elif msg.get("id") == msg_id:
                if not msg.get("success", False):
                    raise RuntimeError(f"{payload.get('type')} failed: {msg.get('error')}")
                return msg.get("result")

    async def set_text(self, entity_id, value, limit):
        await self.call({
            "type": "call_service",
            "domain": "input_text",
            "service": "set_value",
            "target": {"entity_id": entity_id},
            "service_data": {"value": ascii_clip(value, limit)},
        })

    async def next_event(self, timeout):
        if self.events:
            return self.events.pop(0)
        while True:
            msg = json.loads(await asyncio.wait_for(self.ws.recv(), timeout))
            if msg.get("type") == "event":
                return msg

    async def close(self):
        await self.ws.close()


# ---------------------------------------------------------------- permission flow

def describe(payload):
    tool = payload.get("tool_name", "Tool")
    ti = payload.get("tool_input") or {}
    what = (ti.get("command") or ti.get("file_path") or ti.get("url")
            or ti.get("pattern") or ti.get("description") or json.dumps(ti))
    project = Path(payload.get("cwd") or os.getcwd()).name
    return tool, f"{project} | {tool}: {what}"


async def ask_display(tool, detail, timeout=DECISION_TIMEOUT):
    """Returns 'approve', 'reject' or None (no answer / failure)."""
    request_id = uuid.uuid4().hex[:12]
    client = await HAWebSocket.open()
    try:
        # Subscribe before showing the request so a fast tap can't be missed.
        await client.call({"type": "subscribe_events", "event_type": EVENT_TYPE})
        await client.set_text(ENT_REQUEST, request_id, 64)
        await client.set_text(ENT_DETAIL, detail, 255)
        await client.set_text(ENT_STATUS, "Pending Approval", 100)

        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while (remaining := deadline - loop.time()) > 0:
            try:
                msg = await client.next_event(remaining)
            except asyncio.TimeoutError:
                break
            data = msg.get("event", {}).get("data", {})
            if data.get("request_id") != request_id:
                continue  # a tap meant for an older/other request
            if data.get("action") in ("approve", "reject"):
                return data["action"]
        return None
    finally:
        try:
            await client.close()
        except Exception:
            pass


def handle_permission_request(payload):
    tool, detail = describe(payload)
    if tool in SKIP_TOOLS or not display_ready():
        return
    decision = None
    try:
        decision = asyncio.run(ask_display(tool, detail))
    except BaseException:
        decision = None
    finally:
        # Clear the request id so a late tap can't match anything.
        if decision == "approve":
            set_display("Working", f"Approved: {detail}", "")
        elif decision == "reject":
            set_display("Working", f"Rejected: {detail}", "")
        else:
            set_display("Needs Input", f"Answer at the computer: {detail}", "")

    if decision == "approve":
        out = {"behavior": "allow"}
    elif decision == "reject":
        out = {"behavior": "deny", "message": "Rejected on the Waveshare display"}
    else:
        return  # no output -> normal permission prompt
    print(json.dumps({"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": out}}))


def run_hook():
    if not HA_TOKEN:
        return
    try:
        payload = json.loads(sys.stdin.read() or "{}")
    except ValueError:
        return
    event = payload.get("hook_event_name", "")
    project = Path(payload.get("cwd") or os.getcwd()).name

    if event == "PermissionRequest":
        handle_permission_request(payload)
    elif event == "UserPromptSubmit":
        set_display("Working", f"{project} | {payload.get('prompt', '')}", "")
    elif event == "Stop":
        set_display("Ready", f"{project} | Finished. Waiting for the next prompt.", "")
    elif event == "Notification" and payload.get("notification_type") == "idle_prompt":
        set_display("Needs Input", f"{project} | {payload.get('message', 'Claude is waiting for you')}", "")


# ---------------------------------------------------------------- setup / check / test

async def _setup(create):
    client = await HAWebSocket.open()
    try:
        existing = {item.get("id") for item in await client.call({"type": "input_text/list"}) or []}
        states = await client.call({"type": "get_states"}) or []
        entity_ids = {s["entity_id"] for s in states}
        for name, max_len in HELPERS.items():
            ent = f"input_text.{name}"
            if ent in entity_ids or name in existing:
                print(f"ok       {ent}")
            elif create:
                await client.call({"type": "input_text/create", "name": name, "max": max_len, "min": 0, "mode": "text"})
                print(f"created  {ent}")
            else:
                print(f"MISSING  {ent}  (run: setup)")
    finally:
        await client.close()


def cmd_setup(create):
    if not HA_TOKEN:
        sys.exit(f"No HA_TOKEN in {CONFIG_FILE}")
    print(f"Home Assistant: {HA_URL}")
    try:
        asyncio.run(_setup(create))
    except Exception as e:
        sys.exit(f"Failed: {e}")
    if create:
        set_display("Ready", "Bridge connected. Waiting for Claude Code...", "")


def cmd_test():
    if not HA_TOKEN:
        sys.exit(f"No HA_TOKEN in {CONFIG_FILE}")
    print("Showing a test request on the display; tap APPROVE or REJECT (60s)...")
    try:
        result = asyncio.run(ask_display("Bash", "bridge test | Bash: echo hello from the display", timeout=60))
    except Exception as e:
        sys.exit(f"Failed: {e}")
    set_display("Ready", f"Test finished: {result or 'no tap'}", "")
    print(f"Display answered: {result or 'nothing (timed out)'}")


def main():
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))  # run cleanup if Claude cancels the hook
    cmd = sys.argv[1] if len(sys.argv) > 1 else "hook"
    if cmd == "hook":
        try:
            run_hook()
        except BaseException:
            pass  # a hook must never break Claude Code
    elif cmd == "setup":
        cmd_setup(create=True)
    elif cmd == "check":
        cmd_setup(create=False)
    elif cmd == "test":
        cmd_test()
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
