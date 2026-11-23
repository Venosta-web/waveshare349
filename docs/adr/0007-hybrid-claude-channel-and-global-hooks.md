---
status: accepted
---

# Hybrid Claude Channel and Global Hooks

Claude Code's first-party Remote Control (`/rc`) has no public device-registration API for third-party hardware, while replacing `~/.claude/settings.json` hooks with a Channel-only MCP server would drop passive monitoring for Claude Desktop and ad-hoc CLI sessions. Keep the global hooks in `claude_bridge/` to own `PermissionRequest` (`Approve`/`Reject`) and passive status across all sessions, and extend `claude_ha_bridge.py` with a global `claude/channel` MCP server mode for two-way device interaction in channel-enabled CLI sessions.

Each Channel MCP process connects directly to Home Assistant (no background daemon), routes **Voice Satellite** prompts from `ha_assist_tools.yaml` to the most recently active Channel session, and exposes a `reply_to_device` tool that writes concise `<= 255`-character summaries to `input_text.claude_detail` (with TTS readout for device-initiated turns). Keep the existing LVGL AI card variants unchanged in this iteration rather than adding touchscreen quick-reply buttons across every `claude/*_card.yaml` variant.
