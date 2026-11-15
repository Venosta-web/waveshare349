"""Tests for rate-limit detection and formatting in claude_ha_bridge."""
from datetime import datetime
import io
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import claude_ha_bridge as bridge  # noqa: E402


def test_is_rate_limit_text():
    assert bridge.is_rate_limit_text("5-hour limit reached · Auto-resuming at 4:31 PM")
    assert bridge.is_rate_limit_text("You've hit your limit · resets 5pm")
    assert bridge.is_rate_limit_text("API error 429: too many requests")
    assert bridge.is_rate_limit_text("7-day usage limit reached")
    assert not bridge.is_rate_limit_text("Finished implementing the feature.")
    assert not bridge.is_rate_limit_text("Please run pytest on port 4290")


def test_parse_reset_timestamp_time_of_day():
    now = datetime(2026, 11, 15, 14, 15, 0)
    ts = bridge.parse_reset_timestamp("5-hour limit reached. Auto-resuming at 4:31 PM", now)
    assert ts == datetime(2026, 11, 15, 16, 31, 0).timestamp()

    # Past time today wraps to tomorrow
    ts_next = bridge.parse_reset_timestamp("You've hit your limit · resets 1:00 PM", now)
    assert ts_next == datetime(2026, 11, 16, 13, 0, 0).timestamp()


def test_parse_reset_timestamp_relative_and_month():
    now = datetime(2026, 11, 15, 14, 0, 0)
    ts_rel = bridge.parse_reset_timestamp("Rate limit resets in 3h 42m", now)
    assert ts_rel == datetime(2026, 11, 15, 17, 42, 0).timestamp()

    ts_mon = bridge.parse_reset_timestamp("7-day limit reached · resets Nov 20, 2:00 PM", now)
    assert ts_mon == datetime(2026, 11, 20, 14, 0, 0).timestamp()


def test_parse_rate_limit_info_5h_and_7d():
    now = datetime(2026, 11, 15, 14, 15, 0)
    w, rem, rstr, summary = bridge.parse_rate_limit_info(
        "5-hour limit reached · Auto-resuming at 4:31 PM", now_dt=now
    )
    assert w == "5h"
    assert rem == (2 * 3600 + 16 * 60)
    assert rstr == "4:31 PM"
    assert summary == "5-hour usage limit reached"

    w7, rem7, rstr7, summary7 = bridge.parse_rate_limit_info(
        "You've hit your limit · resets Nov 20, 2:00 PM", now_dt=now
    )
    assert w7 == "7d"
    assert rem7 == (4 * 86400 + 23 * 3600 + 45 * 60)
    assert rstr7 == "Nov 20, 2:00 PM"
    assert summary7 == "7-day usage limit reached"


def test_parse_rate_limit_info_uses_structured_rate_limits():
    now = datetime(2026, 11, 15, 14, 0, 0)
    payload = {
        "rate_limits": {
            "five_hour": {"used_percentage": 100.0, "resets_at": now.timestamp() + 1800},
            "seven_day": {"used_percentage": 45.0, "resets_at": now.timestamp() + 400000},
        }
    }
    w, rem, _, _ = bridge.parse_rate_limit_info("rate_limit", payload=payload, now_dt=now)
    assert w == "5h"
    assert rem == 1800


def test_run_hook_stop_failure_rate_limit(monkeypatch, tmp_path):
    calls = []
    monkeypatch.setattr(bridge, "HA_TOKEN", "test-token")
    monkeypatch.setattr(bridge, "OWNER_FILE", tmp_path / "owner.json")
    monkeypatch.setattr(bridge, "request_pending", lambda: False)
    monkeypatch.setattr(
        bridge,
        "set_display",
        lambda status=None, detail=None, request=None, steps=None: calls.append((status, detail)),
    )

    payload = {
        "hook_event_name": "StopFailure",
        "session_id": "s1",
        "cwd": "/tmp/myproj",
        "error_type": "rate_limit",
        "error_message": "5-hour limit reached · resets in 2h 30m",
    }
    monkeypatch.setattr(sys, "stdin", io.StringIO(json.dumps(payload)))
    bridge.run_hook()

    assert len(calls) == 1
    status, detail = calls[0]
    assert status == "Rate Limited"
    assert detail.startswith("5h|")
    assert "|myproj | 5-hour usage limit reached" in detail


def test_extract_seven_day_remaining():
    now = datetime(2026, 11, 15, 14, 0, 0)  # Sunday
    payload = {
        "rate_limits": {
            "five_hour": {"used_percentage": 100.0, "resets_at": now.timestamp() + 1800},
            "seven_day": {"used_percentage": 42.0, "resets_at": datetime(2026, 11, 19, 14, 0, 0).timestamp()},
        }
    }
    sd = bridge.extract_seven_day_remaining(payload=payload, now_dt=now)
    assert sd == (58, "Thu 2:00 PM")

    # Exhausted 7d returns None
    payload["rate_limits"]["seven_day"]["used_percentage"] = 100.0
    assert bridge.extract_seven_day_remaining(payload=payload, now_dt=now) is None


def test_run_hook_includes_7d_when_5h_limited(monkeypatch, tmp_path):
    calls = []
    monkeypatch.setattr(bridge, "HA_TOKEN", "test-token")
    monkeypatch.setattr(bridge, "OWNER_FILE", tmp_path / "owner.json")
    monkeypatch.setattr(bridge, "request_pending", lambda: False)
    monkeypatch.setattr(
        bridge,
        "set_display",
        lambda status=None, detail=None, request=None, steps=None: calls.append((status, detail)),
    )

    now = datetime.now()
    payload = {
        "hook_event_name": "StopFailure",
        "session_id": "s1",
        "cwd": "/tmp/myproj",
        "error_type": "rate_limit",
        "error_message": "5-hour limit reached · resets in 2h 30m",
        "rate_limits": {
            "five_hour": {"used_percentage": 100.0, "resets_at": now.timestamp() + 9000},
            "seven_day": {"used_percentage": 42.0, "resets_at": now.timestamp() + 3 * 86400},
        },
    }
    monkeypatch.setattr(sys, "stdin", io.StringIO(json.dumps(payload)))
    bridge.run_hook()

    assert len(calls) == 1
    status, detail = calls[0]
    assert status == "Rate Limited"
    assert "|7d=58;" in detail
    assert detail.endswith("|myproj | 5-hour usage limit reached")
