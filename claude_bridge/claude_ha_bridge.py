#!/usr/bin/env python3
"""Claude Code <-> Home Assistant <-> Waveshare display bridge.

Subcommands:
  hook        Claude Code hook entry point (reads hook JSON on stdin).
  channel     Claude Code MCP Channel server (stdio JSON-RPC 2.0 + HA WebSocket).
  statusline  Optional: read Claude Code statusLine JSON on stdin to cache rate_limits.
  setup       Create the input_text helpers in Home Assistant if missing.
  check       Verify the token and that the helpers exist.
  test        Send a fake permission request to the display and print the tap.

Config: ~/.config/claude-ha-bridge/config.env (HA_URL=..., HA_TOKEN=...).
Optional TTS: CLAUDE_TTS_ENTITY=tts.piper, CLAUDE_TTS_SPEAKER=media_player....

Safety rule: the hook never denies on its own. If the display does not answer
in time, Home Assistant is unreachable, or anything fails, it prints nothing
and Claude Code falls back to its normal permission prompt.
"""
import asyncio
from datetime import datetime, timedelta
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
RATE_LIMIT_CACHE = Path.home() / ".cache" / "claude-ha-bridge" / "rate_limits.json"
ACTIVE_CHANNEL_FILE = Path.home() / ".cache" / "claude-ha-bridge" / "active_channel.json"
CHANNEL_REPLY_FILE = Path.home() / ".cache" / "claude-ha-bridge" / "channel_reply.json"
CHANNEL_REPLY_TTL = 15
OWNER_MAX_AGE = 12 * 3600  # forget a Needs Input whose session never came back
WRAP_UP_TOOL = "mcp__ccd_turn__wrap_up"  # desktop app's end-of-turn box
EVENT_TYPE = "esphome.claude_command"
ENT_STATUS = "input_text.claude_status"
ENT_DETAIL = "input_text.claude_detail"
ENT_REQUEST = "input_text.claude_request"
ENT_STEPS = "input_text.claude_steps"
HELPERS = {  # name -> max length; entity id becomes input_text.<name>
    "claude_status": 100,
    "claude_detail": 255,
    "claude_request": 64,
    "claude_steps": 255,
}
DECISION_TIMEOUT = float(os.environ.get("CLAUDE_HA_DECISION_TIMEOUT", "110"))
CONNECT_TIMEOUT = 3
# Tools whose prompt needs more than yes/no; shown as Needs Input instead (PreToolUse).
SKIP_TOOLS = {"AskUserQuestion"}


def load_config_all():
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
    tts_entity = os.environ.get("CLAUDE_TTS_ENTITY") or cfg.get("CLAUDE_TTS_ENTITY") or ""
    tts_speaker = os.environ.get("CLAUDE_TTS_SPEAKER") or cfg.get("CLAUDE_TTS_SPEAKER") or ""
    return url, token, tts_entity, tts_speaker


def load_config():
    url, token, _, _ = load_config_all()
    return url, token


HA_URL, HA_TOKEN, CLAUDE_TTS_ENTITY, CLAUDE_TTS_SPEAKER = load_config_all()
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


def rest_tts_speak(message):
    """Best-effort TTS readout on the configured Home Assistant speaker."""
    if not (HA_TOKEN and CLAUDE_TTS_ENTITY and CLAUDE_TTS_SPEAKER and message):
        return False
    req = urllib.request.Request(
        f"{HA_URL}/api/services/tts/speak",
        data=json.dumps({
            "entity_id": CLAUDE_TTS_ENTITY,
            "media_player_entity_id": CLAUDE_TTS_SPEAKER,
            "message": ascii_clip(message, 255),
        }).encode(),
        headers={"Authorization": f"Bearer {HA_TOKEN}", "Content-Type": "application/json"},
        method="POST",
    )
    try:
        urllib.request.urlopen(req, timeout=3).read()
        return True
    except Exception:
        return False


def set_display(status=None, detail=None, request=None, steps=None):
    """Best effort; stops at the first failure so a down HA costs one timeout."""
    if not HA_TOKEN:
        return
    for ent, value, limit in (
        (ENT_REQUEST, request, 64),
        (ENT_STEPS, steps, 255),
        (ENT_DETAIL, detail, 255),
        (ENT_STATUS, status, 100),
    ):
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

def _write_json_atomic(path, payload):
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_suffix(f".{os.getpid()}.tmp")
        tmp.write_text(json.dumps(payload))
        tmp.replace(path)
    except OSError:
        pass


def _read_json(path):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return None


def _pid_alive(pid):
    if not isinstance(pid, int) or pid <= 0:
        return False
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def recent_channel_reply(project):
    """True when reply_to_device recently posted a reply for this project."""
    data = _read_json(CHANNEL_REPLY_FILE)
    if not data:
        return False
    if time.time() - data.get("at", 0) > CHANNEL_REPLY_TTL:
        return False
    return data.get("project") == project


def load_owner():
    owner = _read_json(OWNER_FILE)
    if not owner or time.time() - owner.get("at", 0) > OWNER_MAX_AGE:
        return None
    return owner


def save_owner(session, status, detail):
    _write_json_atomic(OWNER_FILE, {"session": session, "status": status, "detail": detail, "at": time.time()})


def clear_owner():
    try:
        OWNER_FILE.unlink()
    except OSError:
        pass


def show(session, status, detail, needs_input=False, after_request=False, steps=None):
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
        set_display(status, detail, "", steps=steps)
    elif not request_pending():
        set_display(status, detail, steps=steps)


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


# ---------------------------------------------------------------- rate limits

_RL_KEYWORDS = (
    "rate_limit",
    "rate limit",
    "limit reached",
    "usage limit",
    "too many requests",
    "hit your limit",
    "auto-resuming",
)
_MONTHS = {
    "jan": 1, "january": 1, "feb": 2, "february": 2, "mar": 3, "march": 3,
    "apr": 4, "april": 4, "may": 5, "jun": 6, "june": 6,
    "jul": 7, "july": 7, "aug": 8, "august": 8, "sep": 9, "sept": 9, "september": 9,
    "oct": 10, "october": 10, "nov": 11, "november": 11, "dec": 12, "december": 12,
}
_RE_REL = re.compile(
    r"resets?\s+in\s+(?:(\d+)\s*d(?:ays?)?\s*)?(?:(\d+)\s*h(?:ours?|rs?)?\s*)?(?:(\d+)\s*m(?:ins?|inutes?)?)?",
    re.I,
)
_RE_MONTH_TIME = re.compile(
    r"(?:resets?|resuming)(?:\s+at)?\s+([A-Za-z]{3,9})\s+(\d{1,2})(?:st|nd|rd|th)?,?\s+(?:at\s+)?(\d{1,2})(?::(\d{2}))?\s*(am|pm)?",
    re.I,
)
_RE_TIME = re.compile(
    r"(?:resets?|resuming|at)\s+(?:at\s+)?(\d{1,2})(?::(\d{2})\s*(am|pm)?|\s*(am|pm))",
    re.I,
)


def is_rate_limit_text(text):
    low = (text or "").lower()
    return any(k in low for k in _RL_KEYWORDS) or bool(re.search(r"\b429\b", low))


def _to_24h(hour, ampm):
    if ampm:
        ap = ampm.lower()
        if ap == "pm" and hour != 12:
            return hour + 12
        if ap == "am" and hour == 12:
            return 0
    return hour


def parse_reset_timestamp(text, now_dt=None):
    """Best-effort parse of a Claude rate-limit reset time into a Unix timestamp."""
    now_dt = now_dt or datetime.now()
    s = text or ""

    m = _RE_REL.search(s)
    if m and any(m.groups()):
        days = int(m.group(1) or 0)
        hours = int(m.group(2) or 0)
        mins = int(m.group(3) or 0)
        return (now_dt + timedelta(days=days, hours=hours, minutes=mins)).timestamp()

    m = _RE_MONTH_TIME.search(s)
    if m:
        month = _MONTHS.get(m.group(1).lower())
        if month:
            day = int(m.group(2))
            hour = _to_24h(int(m.group(3)), m.group(5))
            minute = int(m.group(4) or 0)
            year = now_dt.year
            try:
                target = now_dt.replace(year=year, month=month, day=day, hour=hour, minute=minute, second=0, microsecond=0)
                if target <= now_dt - timedelta(days=1):
                    target = target.replace(year=year + 1)
                return target.timestamp()
            except ValueError:
                pass

    m = _RE_TIME.search(s)
    if m:
        hour = _to_24h(int(m.group(1)), m.group(3) or m.group(4))
        minute = int(m.group(2) or 0)
        if 0 <= hour <= 23 and 0 <= minute <= 59:
            target = now_dt.replace(hour=hour, minute=minute, second=0, microsecond=0)
            if target <= now_dt:
                target += timedelta(days=1)
            return target.timestamp()

    return None


def _load_cached_rate_limits():
    try:
        data = json.loads(RATE_LIMIT_CACHE.read_text())
        if time.time() - data.get("at", 0) <= 600:
            return data.get("rate_limits") or {}
    except (OSError, ValueError):
        pass
    return {}


def save_rate_limits_cache(rate_limits):
    if not isinstance(rate_limits, dict):
        return
    try:
        RATE_LIMIT_CACHE.parent.mkdir(parents=True, exist_ok=True)
        tmp = RATE_LIMIT_CACHE.with_suffix(f".{os.getpid()}.tmp")
        tmp.write_text(json.dumps({"rate_limits": rate_limits, "at": time.time()}))
        tmp.replace(RATE_LIMIT_CACHE)
    except OSError:
        pass


def parse_rate_limit_info(text, payload=None, now_dt=None):
    """Return (window, remaining_secs, reset_clock_str, summary_text)."""
    now_dt = now_dt or datetime.now()
    now_ts = now_dt.timestamp()
    payload = payload or {}
    rl = payload.get("rate_limits") or _load_cached_rate_limits()

    window = None
    resets_at = None
    for key, wname in (("seven_day", "7d"), ("five_hour", "5h")):
        entry = rl.get(key) if isinstance(rl, dict) else None
        if isinstance(entry, dict) and float(entry.get("used_percentage") or 0) >= 99.9:
            ra = entry.get("resets_at")
            if ra and float(ra) > now_ts:
                window = wname
                resets_at = float(ra)
                break

    if resets_at is None:
        resets_at = parse_reset_timestamp(text, now_dt)

    low = (text or "").lower()
    delta = (resets_at - now_ts) if resets_at is not None else None
    if window is None:
        if any(k in low for k in ("7-day", "7 day", "7d", "weekly")):
            window = "7d"
        elif any(k in low for k in ("5-hour", "5 hour", "5h")):
            window = "5h"
        elif delta is not None and delta > 5.5 * 3600:
            window = "7d"
        else:
            window = "5h"

    if resets_at is None:
        resets_at = now_ts + (7 * 86400 if window == "7d" else 5 * 3600)

    remaining_secs = max(0, int(round(resets_at - now_ts)))
    reset_dt = datetime.fromtimestamp(resets_at)
    if reset_dt.date() == now_dt.date():
        reset_clock_str = reset_dt.strftime("%-I:%M %p")
    else:
        reset_clock_str = reset_dt.strftime("%b %-d, %-I:%M %p")

    summary = "7-day usage limit reached" if window == "7d" else "5-hour usage limit reached"
    return window, remaining_secs, reset_clock_str, summary


def extract_seven_day_remaining(payload=None, now_dt=None):
    """Return (left_pct, reset_str) if seven_day quota still has room, else None."""
    now_dt = now_dt or datetime.now()
    now_ts = now_dt.timestamp()
    payload = payload or {}
    rl = payload.get("rate_limits") or _load_cached_rate_limits()
    sd = rl.get("seven_day") if isinstance(rl, dict) else None
    if not isinstance(sd, dict) or sd.get("used_percentage") is None:
        return None
    used_pct = float(sd["used_percentage"])
    if used_pct >= 99.9:
        return None
    left_pct = max(1, min(100, int(round(100.0 - used_pct))))
    reset_str = ""
    ra = sd.get("resets_at")
    if ra and float(ra) > now_ts:
        rdt = datetime.fromtimestamp(float(ra))
        if (rdt.date() - now_dt.date()).days < 7:
            reset_str = rdt.strftime("%a %-I:%M %p")
        else:
            reset_str = rdt.strftime("%b %-d, %-I:%M %p")
    return left_pct, reset_str


def show_rate_limit(project, window, remaining_secs, reset_clock_str, summary, seven_day=None):
    if not HA_TOKEN:
        return
    clear_owner()
    body = f"{project} | {summary}" if project else summary
    sd_part = f"7d={seven_day[0]};{seven_day[1]}|" if (window == "5h" and seven_day) else ""
    detail = f"{window}|{int(remaining_secs)}|{reset_clock_str}|{sd_part}{body}"
    if not request_pending():
        set_display("Rate Limited", detail, steps="")


STEP_TOOLS = {"TodoWrite", "TaskCreate", "TaskUpdate"}
TASK_ID_RE = re.compile(r"(?:Task\s*#?|id[\"']?\s*[:=]\s*[\"']?)(\d+)", re.I)


def _clean_step(text):
    return " ".join(str(text or "").replace("|", "/").split())


def format_steps(tasks, only_if_active=False):
    """Format task list as '<cur>/<total>|<current>|<next>' (<= 255 ASCII chars)."""
    live = [t for t in tasks if t.get("status") != "deleted" and _clean_step(t.get("content") or t.get("subject"))]
    if not live:
        return ""
    if only_if_active and all(t.get("status") == "completed" for t in live):
        return ""

    cur_idx = next((i for i, t in enumerate(live) if t.get("status") == "in_progress"), None)
    if cur_idx is None:
        cur_idx = next((i for i, t in enumerate(live) if t.get("status") == "pending"), None)
    if cur_idx is None:
        cur_idx = len(live) - 1

    cur_text = _clean_step(live[cur_idx].get("content") or live[cur_idx].get("subject"))
    next_idx = next(
        (i for i in range(cur_idx + 1, len(live)) if live[i].get("status") in ("pending", "in_progress")),
        None,
    )
    next_text = _clean_step(live[next_idx].get("content") or live[next_idx].get("subject")) if next_idx is not None else ""
    payload = f"{cur_idx + 1}/{len(live)}|{ascii_clip(cur_text, 115)}|{ascii_clip(next_text, 115)}"
    return ascii_clip(payload, 255)


def extract_steps(transcript_path, tool_name=None, tool_input=None, only_if_active=False):
    """Reconstruct current task steps from TodoWrite / TaskCreate / TaskUpdate."""
    if tool_name == "TodoWrite" and isinstance(tool_input, dict) and isinstance(tool_input.get("todos"), list):
        return format_steps(tool_input["todos"], only_if_active=only_if_active)

    tasks = []
    by_id = {}
    pending_creates = {}
    next_seq = 1
    try:
        lines = Path(transcript_path).read_text(errors="replace").splitlines()
    except (OSError, TypeError):
        lines = []

    for line in lines[-2000:]:
        try:
            row = json.loads(line)
        except ValueError:
            continue
        content = (row.get("message") or {}).get("content")
        if not isinstance(content, list):
            continue
        for c in content:
            if not isinstance(c, dict):
                continue
            ctype = c.get("type")
            if row.get("type") == "assistant" and ctype == "tool_use":
                name = c.get("name")
                inp = c.get("input") or {}
                if name == "TodoWrite" and isinstance(inp.get("todos"), list):
                    tasks = [dict(t) for t in inp["todos"] if isinstance(t, dict)]
                    by_id.clear()
                    pending_creates.clear()
                elif name == "TaskCreate":
                    task = {"subject": inp.get("subject", ""), "status": inp.get("status", "pending")}
                    tasks.append(task)
                    if c.get("id"):
                        pending_creates[c["id"]] = task
                    else:
                        by_id[str(next_seq)] = task
                        next_seq += 1
                elif name == "TaskUpdate":
                    tid = str(inp.get("taskId") or "")
                    target = by_id.get(tid)
                    if target is not None:
                        for k in ("status", "subject", "content"):
                            if k in inp and inp[k] is not None:
                                target[k] = inp[k]
            elif row.get("type") == "user" and ctype == "tool_result":
                tuid = c.get("tool_use_id")
                if tuid in pending_creates:
                    task = pending_creates.pop(tuid)
                    rc = c.get("content")
                    raw = rc if isinstance(rc, str) else json.dumps(rc or "")
                    m = TASK_ID_RE.search(raw)
                    tid = m.group(1) if m else str(next_seq)
                    next_seq = max(next_seq + 1, int(tid) + 1 if tid.isdigit() else next_seq + 1)
                    by_id[tid] = task

    for tid, task in pending_creates.items():
        by_id[str(next_seq)] = task
        next_seq += 1

    if tool_name == "TaskCreate" and isinstance(tool_input, dict):
        subj = tool_input.get("subject", "")
        if subj and not any((t.get("subject") == subj) for t in tasks[-1:]):
            tasks.append({"subject": subj, "status": tool_input.get("status", "pending")})
    elif tool_name == "TaskUpdate" and isinstance(tool_input, dict):
        tid = str(tool_input.get("taskId") or "")
        if tid in by_id:
            for k in ("status", "subject", "content"):
                if k in tool_input and tool_input[k] is not None:
                    by_id[tid][k] = tool_input[k]

    return format_steps(tasks, only_if_active=only_if_active)


_INTERNAL_TAG_RE = re.compile(
    r"<(?:system-reminder|ide_opened_file|ide_selection|local-command-caveat|"
    r"local-command-stdout|local-command-stderr|user-prompt-submit-hook|"
    r"command-message)\b[^>]*>.*?</(?:system-reminder|ide_opened_file|"
    r"ide_selection|local-command-caveat|local-command-stdout|"
    r"local-command-stderr|user-prompt-submit-hook|command-message)>",
    re.S | re.I,
)
_TASK_NOTIF_RE = re.compile(r"<task-notification\b[^>]*>(.*?)(?:</task-notification>|$)", re.S | re.I)
_TEAMMATE_MSG_RE = re.compile(r"<teammate-message\b([^>]*)>(.*?)(?:</teammate-message>|$)", re.S | re.I)


def _xml_tag(text, tag):
    m = re.search(rf"<{tag}\b[^>]*>(.*?)</{tag}>", text, re.S | re.I)
    return " ".join(m.group(1).split()) if m else ""


def _xml_attr(attrs, name):
    m = re.search(rf'\b{name}\s*=\s*"([^"]*)"', attrs, re.I)
    return " ".join(m.group(1).split()) if m else ""


def format_prompt_detail(prompt):
    """Turn raw UserPromptSubmit text (including Claude Code XML wrappers) into readable copy."""
    raw = str(prompt or "").strip()
    if not raw:
        return "Working on task"

    cleaned = _INTERNAL_TAG_RE.sub(" ", raw)

    # Slash command invocations: <command-name>/review</command-name> [<command-args>...</command-args>]
    cmd_name = _xml_tag(cleaned, "command-name")
    if cmd_name:
        cmd_args = _xml_tag(cleaned, "command-args")
        without_cmd = re.sub(r"<command-(?:name|args)\b[^>]*>.*?</command-(?:name|args)>", " ", cleaned, flags=re.S | re.I)
        rest = " ".join(without_cmd.split())
        if rest:
            return rest
        return f"{cmd_name} {cmd_args}".strip()

    # Background task / subagent completion notifications
    notif_match = _TASK_NOTIF_RE.search(cleaned)
    if notif_match:
        without_notif = _TASK_NOTIF_RE.sub(" ", cleaned)
        rest = " ".join(without_notif.split())
        if rest:
            return rest
        body = notif_match.group(1)
        summary = _xml_tag(body, "summary")
        if summary:
            return summary
        status = _xml_tag(body, "status").lower()
        if status in ("killed", "stopped", "cancelled", "canceled"):
            return "Background task stopped"
        if status:
            return f"Background task {status}"
        return "Continuing after background task"

    # Agent team teammate messages
    tm_match = _TEAMMATE_MSG_RE.search(cleaned)
    if tm_match:
        without_tm = _TEAMMATE_MSG_RE.sub(" ", cleaned)
        rest = " ".join(without_tm.split())
        if rest:
            return rest
        attrs, body = tm_match.group(1), tm_match.group(2)
        teammate = _xml_attr(attrs, "teammate_id")
        summary = _xml_attr(attrs, "summary")
        if teammate and summary:
            return f"From {teammate}: {summary}"
        if summary:
            return summary
        if teammate:
            return f"Message from {teammate}"
        body_text = " ".join(body.split())
        return body_text or "Teammate message"

    text = " ".join(cleaned.split())
    return text or "Working on task"


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
    transcript = payload.get("transcript_path")

    if event == "PermissionRequest":
        handle_permission_request(payload)
    elif event == "UserPromptSubmit":
        steps = extract_steps(transcript, only_if_active=True)
        detail_text = format_prompt_detail(payload.get("prompt", ""))
        show(session, "Working", f"{project} | {detail_text}", steps=steps)
    elif event == "PreToolUse" and tool == "AskUserQuestion":
        steps = extract_steps(transcript)
        show(session, "Needs Input", f"{project} | {describe_questions(payload.get('tool_input'))}",
             needs_input=True, steps=steps)
    elif event == "PostToolUse" and tool == "AskUserQuestion":
        steps = extract_steps(transcript)
        show(session, "Working", f"{project} | Answered. Back to work.", steps=steps)
    elif event == "PostToolUse" and tool in STEP_TOOLS:
        steps = extract_steps(transcript, tool_name=tool, tool_input=payload.get("tool_input"))
        owner = load_owner()
        if not request_pending() and not (owner and owner.get("session") != session):
            set_display(steps=steps)
    elif event == "Stop":
        ask, text = last_turn(transcript)
        combined = text or payload.get("last_assistant_message", "")
        if payload.get("error") == "rate_limit" or is_rate_limit_text(combined):
            w, rem, rstr, summary = parse_rate_limit_info(combined, payload)
            sd = extract_seven_day_remaining(payload) if w == "5h" else None
            show_rate_limit(project, w, rem, rstr, summary, seven_day=sd)
            return
        steps = extract_steps(transcript)
        waiting_on = ask or guess_waiting_on(combined)
        if waiting_on:
            show(session, "Needs Input", f"{project} | {waiting_on}", needs_input=True, steps=steps)
        elif recent_channel_reply(project):
            # reply_to_device already wrote the turn's concise summary to the display.
            owner = load_owner()
            if not request_pending() and not (owner and owner.get("session") != session):
                set_display(steps=steps)
        else:
            show(session, "Done", f"{project} | Finished. Waiting for the next prompt.", steps=steps)
    elif event == "StopFailure":
        err_type = (payload.get("error_type") or payload.get("error") or "").lower()
        err_msg = payload.get("error_message") or payload.get("message") or ""
        if not err_msg:
            _, err_msg = last_turn(transcript)
        if err_type == "rate_limit" or is_rate_limit_text(err_msg):
            w, rem, rstr, summary = parse_rate_limit_info(err_msg, payload)
            sd = extract_seven_day_remaining(payload) if w == "5h" else None
            show_rate_limit(project, w, rem, rstr, summary, seven_day=sd)
        else:
            label = err_msg or err_type or "API error"
            show(session, "Needs Input", f"{project} | {label}", needs_input=True)
    elif event == "Notification":
        kind = payload.get("notification_type")
        msg = payload.get("message", "")
        owner = load_owner()
        if kind == "rate_limit" or is_rate_limit_text(msg):
            w, rem, rstr, summary = parse_rate_limit_info(msg, payload)
            sd = extract_seven_day_remaining(payload) if w == "5h" else None
            show_rate_limit(project, w, rem, rstr, summary, seven_day=sd)
        elif kind == "elicitation_dialog":
            show(session, "Needs Input", f"{project} | {msg or 'An MCP server needs your input'}",
                 needs_input=True)
        elif kind == "idle_prompt" and not (owner and owner.get("session") == session):
            # Don't replace this session's own, more specific question.
            show(session, "Needs Input", f"{project} | {msg or 'Claude is waiting for you'}",
                 needs_input=True)


# ---------------------------------------------------------------- MCP Channel server

CHANNEL_SERVER_NAME = "waveshare-display"
CHANNEL_INSTRUCTIONS = (
    f'Messages from the Waveshare 3.49 display / Voice Satellite arrive as '
    f'<channel source="{CHANNEL_SERVER_NAME}" ...>. '
    f'When replying to a device-originated message (or when asked to notify the display), '
    f'call reply_to_device with a concise <=255 char ASCII summary.'
)
CHANNEL_TOOLS = [
    {
        "name": "reply_to_device",
        "description": (
            "Send a concise reply (<=255 ASCII chars) to the Waveshare 3.49 display "
            "and optionally speak it via the Voice Satellite."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "message": {
                    "type": "string",
                    "description": "Concise summary to show on the display (max 255 chars).",
                },
                "status": {
                    "type": "string",
                    "enum": ["Done", "Needs Input", "Working"],
                    "description": "Display card status (default: Done).",
                },
                "speak": {
                    "type": "boolean",
                    "description": "Speak via Voice Satellite TTS (defaults to true for device-initiated turns).",
                },
            },
            "required": ["message"],
        },
    }
]
CHANNEL_PROMPT_ACTIONS = {"prompt", "reply", "answer"}


class ClaudeChannelServer:
    """MCP Channel server (claude/channel) bridging HA Voice Satellite <-> Claude Code."""

    def __init__(self, project=None, emit_fn=None):
        self.channel_id = uuid.uuid4().hex[:12]
        self.project = project or Path(os.getcwd()).name
        self.pid = os.getpid()
        self.emit_fn = emit_fn or self._stdout_emit
        self.device_initiated = False

    @staticmethod
    def _stdout_emit(payload):
        sys.stdout.write(json.dumps(payload) + "\n")
        sys.stdout.flush()

    def claim_active(self):
        _write_json_atomic(
            ACTIVE_CHANNEL_FILE,
            {"channel_id": self.channel_id, "project": self.project, "pid": self.pid, "at": time.time()},
        )

    def is_active(self):
        data = _read_json(ACTIVE_CHANNEL_FILE)
        if not data:
            self.claim_active()
            return True
        if data.get("channel_id") == self.channel_id:
            return True
        if not _pid_alive(data.get("pid")):
            self.claim_active()
            return True
        return False

    def release_active(self):
        data = _read_json(ACTIVE_CHANNEL_FILE)
        if data and data.get("channel_id") == self.channel_id:
            try:
                ACTIVE_CHANNEL_FILE.unlink()
            except OSError:
                pass

    def handle_ha_event(self, data):
        """Process an esphome.claude_command event; returns True if pushed to Claude."""
        action = str((data or {}).get("action", "")).lower()
        if action not in CHANNEL_PROMPT_ACTIONS:
            return False
        text = str(data.get("text") or data.get("prompt") or data.get("message") or "").strip()
        if not text or not self.is_active():
            return False
        self.device_initiated = True
        if not request_pending():
            clear_owner()
            set_display("Working", f"{self.project} | {text}")
        self.emit_fn({
            "jsonrpc": "2.0",
            "method": "notifications/claude/channel",
            "params": {
                "content": text,
                "meta": {
                    "source": CHANNEL_SERVER_NAME,
                    "action": action,
                    "project": self.project,
                },
            },
        })
        return True

    def _call_reply_to_device(self, args):
        message = str((args or {}).get("message") or "").strip()
        if not message:
            return {"content": [{"type": "text", "text": "Error: message is required."}], "isError": True}
        status = args.get("status") or "Done"
        if status not in ("Done", "Needs Input", "Working"):
            status = "Done"
        speak = args.get("speak")
        should_speak = self.device_initiated if speak is None else bool(speak)
        self.device_initiated = False
        self.claim_active()

        detail = f"{self.project} | {message}"
        if status == "Needs Input":
            save_owner(f"channel:{self.channel_id}", status, detail)
        else:
            clear_owner()
        _write_json_atomic(
            CHANNEL_REPLY_FILE,
            {"project": self.project, "status": status, "detail": detail, "at": time.time()},
        )
        if not request_pending():
            set_display(status, detail)
        spoken = rest_tts_speak(message) if should_speak else False
        note = "Sent to display" + (" and spoken via TTS." if spoken else ".")
        return {"content": [{"type": "text", "text": note}]}

    def handle_rpc(self, msg):
        """Handle one JSON-RPC 2.0 request/notification; returns response dict or None."""
        if not isinstance(msg, dict):
            return None
        method = msg.get("method", "")
        msg_id = msg.get("id")
        params = msg.get("params") or {}

        if method == "initialize":
            proto = params.get("protocolVersion") or "2024-11-05"
            self.claim_active()
            return {
                "jsonrpc": "2.0",
                "id": msg_id,
                "result": {
                    "protocolVersion": proto,
                    "capabilities": {
                        "experimental": {"claude/channel": {}},
                        "tools": {},
                    },
                    "serverInfo": {"name": CHANNEL_SERVER_NAME, "version": "1.0.0"},
                    "instructions": CHANNEL_INSTRUCTIONS,
                },
            }
        if method == "ping" and msg_id is not None:
            return {"jsonrpc": "2.0", "id": msg_id, "result": {}}
        if method == "tools/list" and msg_id is not None:
            return {"jsonrpc": "2.0", "id": msg_id, "result": {"tools": CHANNEL_TOOLS}}
        if method == "tools/call" and msg_id is not None:
            name = params.get("name")
            if name == "reply_to_device":
                result = self._call_reply_to_device(params.get("arguments") or {})
                return {"jsonrpc": "2.0", "id": msg_id, "result": result}
            return {
                "jsonrpc": "2.0",
                "id": msg_id,
                "error": {"code": -32601, "message": f"Unknown tool: {name}"},
            }
        if msg_id is not None:
            return {
                "jsonrpc": "2.0",
                "id": msg_id,
                "error": {"code": -32601, "message": f"Method not found: {method}"},
            }
        return None


async def _channel_ha_loop(server):
    """Maintain an HA WebSocket subscription and forward device prompts to Claude."""
    backoff = 1
    while True:
        client = None
        try:
            client = await HAWebSocket.open()
            await client.call({"type": "subscribe_events", "event_type": EVENT_TYPE})
            backoff = 1
            while True:
                msg = await client.next_event(timeout=300)
                data = msg.get("event", {}).get("data", {})
                server.handle_ha_event(data)
        except asyncio.CancelledError:
            raise
        except asyncio.TimeoutError:
            backoff = 1
        except Exception:
            await asyncio.sleep(backoff)
            backoff = min(backoff * 2, 30)
        finally:
            if client is not None:
                try:
                    await client.close()
                except Exception:
                    pass


async def _run_channel_async():
    server = ClaudeChannelServer()
    server.claim_active()
    ha_task = asyncio.create_task(_channel_ha_loop(server)) if HA_TOKEN else None
    loop = asyncio.get_running_loop()
    try:
        while True:
            line = await loop.run_in_executor(None, sys.stdin.readline)
            if not line:
                break
            line = line.strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except ValueError:
                continue
            resp = server.handle_rpc(msg)
            if resp is not None:
                server.emit_fn(resp)
    finally:
        if ha_task is not None:
            ha_task.cancel()
            try:
                await ha_task
            except (asyncio.CancelledError, Exception):
                pass
        server.release_active()


def run_channel():
    asyncio.run(_run_channel_async())


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


def run_statusline():
    try:
        payload = json.loads(sys.stdin.read() or "{}")
    except ValueError:
        return
    if "rate_limits" in payload:
        save_rate_limits_cache(payload["rate_limits"])


def main():
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))  # run cleanup if Claude cancels the hook
    cmd = sys.argv[1] if len(sys.argv) > 1 else "hook"
    if cmd == "hook":
        try:
            run_hook()
        except BaseException:
            pass  # a hook must never break Claude Code
    elif cmd == "channel":
        run_channel()
    elif cmd == "statusline":
        try:
            run_statusline()
        except BaseException:
            pass
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
