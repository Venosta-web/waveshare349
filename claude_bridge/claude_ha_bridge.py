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
import time
import urllib.request
import uuid
from pathlib import Path

CONFIG_FILE = Path.home() / ".config" / "claude-ha-bridge" / "config.env"
# Which session's Needs Input is on the display (other sessions must not hide it).
OWNER_FILE = Path.home() / ".cache" / "claude-ha-bridge" / "needs_input_owner.json"
OWNER_MAX_AGE = 12 * 3600  # forget a Needs Input whose session never came back
WRAP_UP_TOOL = "mcp__ccd_turn__wrap_up"  # desktop app's end-of-turn box
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
# Tools whose prompt needs more than yes/no; shown as Needs Input instead (PreToolUse).
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


def request_pending():
    """True while some session is waiting for a tap (request id is set)."""
    req = urllib.request.Request(f"{HA_URL}/api/states/{ENT_REQUEST}",
                                 headers={"Authorization": f"Bearer {HA_TOKEN}"})
    try:
        state = json.loads(urllib.request.urlopen(req, timeout=2).read()).get("state", "")
    except Exception:
        return False
    return state not in ("", "unknown", "unavailable")


# ---------------------------------------------------------------- Needs Input owner

def load_owner():
    try:
        owner = json.loads(OWNER_FILE.read_text())
    except (OSError, ValueError):
        return None
    if time.time() - owner.get("at", 0) > OWNER_MAX_AGE:
        return None
    return owner


def save_owner(session, status, detail):
    try:
        OWNER_FILE.parent.mkdir(parents=True, exist_ok=True)
        tmp = OWNER_FILE.with_suffix(f".{os.getpid()}.tmp")
        tmp.write_text(json.dumps({"session": session, "status": status, "detail": detail, "at": time.time()}))
        tmp.replace(OWNER_FILE)
    except OSError:
        pass


def clear_owner():
    try:
        OWNER_FILE.unlink()
    except OSError:
        pass


def show(session, status, detail, needs_input=False, after_request=False):
    """Status update from a hook.

    Needs Input belongs to the session that raised it: other sessions' Working
    or Done don't replace it; only that session (or a newer Needs Input) does.
    Never clobbers a pending approval, except when called by the hook that
    just finished that approval (after_request), which also clears its id.
    """
    if not HA_TOKEN:
        return
    owner = load_owner()
    if needs_input:
        save_owner(session, status, detail)
    elif owner and owner.get("session") != session:
        if after_request:  # put the waiting session's question back
            set_display(owner["status"], owner["detail"], "")
        return
    elif owner:
        clear_owner()
    if after_request:
        set_display(status, detail, "")
    elif not request_pending():
        set_display(status, detail)


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
    session = payload.get("session_id", "")
    decision = None
    try:
        decision = asyncio.run(ask_display(tool, detail))
    except BaseException:
        decision = None
    finally:
        # Clear the request id so a late tap can't match anything.
        if decision == "approve":
            show(session, "Working", f"Approved: {detail}", after_request=True)
        elif decision == "reject":
            show(session, "Working", f"Rejected: {detail}", after_request=True)
        else:
            show(session, "Needs Input", f"Answer at the computer: {detail}", needs_input=True, after_request=True)

    if decision == "approve":
        out = {"behavior": "allow"}
    elif decision == "reject":
        out = {"behavior": "deny", "message": "Rejected on the Waveshare display"}
    else:
        return  # no output -> normal permission prompt
    print(json.dumps({"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": out}}))


# ---------------------------------------------------------------- what Claude is waiting on

def describe_questions(tool_input):
    """AskUserQuestion input -> 'Question? [A] one / [B] two (+1 more)'."""
    questions = (tool_input or {}).get("questions") or []
    if not questions:
        return "Claude has a question for you"
    q = questions[0]
    labels = [o.get("label", "") for o in q.get("options") or [] if isinstance(o, dict)]
    text = q.get("question", "")
    if labels:
        text += " " + " / ".join(f"[{chr(65 + i)}] {label}" for i, label in enumerate(labels))
    if len(questions) > 1:
        text += f" (+{len(questions) - 1} more)"
    return text


def last_turn(transcript_path):
    """(wrap_up ask or None, last assistant text) for the turn that just ended."""
    try:
        lines = Path(transcript_path).read_text(errors="replace").splitlines()
    except (OSError, TypeError):
        return None, ""
    ask, text = None, ""
    for line in reversed(lines[-2000:]):
        try:
            row = json.loads(line)
        except ValueError:
            continue
        content = (row.get("message") or {}).get("content")
        if row.get("type") == "user":
            # A real prompt (not a tool result) marks the start of the turn.
            if isinstance(content, str) or any(isinstance(c, dict) and c.get("type") == "text" for c in content or []):
                if not row.get("isMeta"):
                    break
            continue
        if row.get("type") != "assistant" or not isinstance(content, list):
            continue
        for c in reversed(content):
            if not isinstance(c, dict):
                continue
            if c.get("type") == "text" and not text and c.get("text", "").strip():
                text = c["text"]
            elif c.get("type") == "tool_use" and c.get("name") == WRAP_UP_TOOL and ask is None:
                ask = ((c.get("input") or {}).get("ask") or "").strip() or None
    return ask, text


FENCE = re.compile(r"```(?:bash|sh|shell|zsh|console)?\n(.*?)```", re.S)


def guess_waiting_on(text):
    """A turn ending in a question, or handing over a command, is waiting on you."""
    commands = [m.group(1).strip() for m in FENCE.finditer(text or "") if m.group(1).strip()]
    prose = FENCE.sub(" ", text or "").strip()
    paragraphs = [p.strip() for p in re.split(r"\n\s*\n", prose) if p.strip()]
    if paragraphs:
        last = re.sub(r"\*\*|__|`", "", paragraphs[-1]).strip()
        if last.endswith("?"):
            return re.split(r"(?<=[.!?])\s+", last)[-1].strip()
    if commands:
        return "Run: " + commands[-1].splitlines()[0]
    return None


def run_hook():
    if not HA_TOKEN:
        return
    try:
        payload = json.loads(sys.stdin.read() or "{}")
    except ValueError:
        return
    event = payload.get("hook_event_name", "")
    session = payload.get("session_id", "")
    project = Path(payload.get("cwd") or os.getcwd()).name
    tool = payload.get("tool_name", "")

    if event == "PermissionRequest":
        handle_permission_request(payload)
    elif event == "UserPromptSubmit":
        show(session, "Working", f"{project} | {payload.get('prompt', '')}")
    elif event == "PreToolUse" and tool == "AskUserQuestion":
        show(session, "Needs Input", f"{project} | {describe_questions(payload.get('tool_input'))}", needs_input=True)
    elif event == "PostToolUse" and tool == "AskUserQuestion":
        show(session, "Working", f"{project} | Answered. Back to work.")
    elif event == "Stop":
        ask, text = last_turn(payload.get("transcript_path"))
        waiting_on = ask or guess_waiting_on(text or payload.get("last_assistant_message", ""))
        if waiting_on:
            show(session, "Needs Input", f"{project} | {waiting_on}", needs_input=True)
        else:
            show(session, "Done", f"{project} | Finished. Waiting for the next prompt.")
    elif event == "Notification":
        kind = payload.get("notification_type")
        owner = load_owner()
        if kind == "elicitation_dialog":
            show(session, "Needs Input", f"{project} | {payload.get('message', 'An MCP server needs your input')}",
                 needs_input=True)
        elif kind == "idle_prompt" and not (owner and owner.get("session") == session):
            # Don't replace this session's own, more specific question.
            show(session, "Needs Input", f"{project} | {payload.get('message', 'Claude is waiting for you')}",
                 needs_input=True)


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
        set_display("Done", "Bridge connected. Waiting for Claude Code...", "")


def cmd_test():
    if not HA_TOKEN:
        sys.exit(f"No HA_TOKEN in {CONFIG_FILE}")
    print("Showing a test request on the display; tap APPROVE or REJECT (60s)...")
    try:
        result = asyncio.run(ask_display("Bash", "bridge test | Bash: echo hello from the display", timeout=60))
    except Exception as e:
        sys.exit(f"Failed: {e}")
    set_display("Done", f"Test finished: {result or 'no tap'}", "")
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
