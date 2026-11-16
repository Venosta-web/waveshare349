# ESP32-S3 480x320 Growspace & AI Display

ESPHome LVGL display for monitoring indoor cultivation environments managed by Growspace Manager in Home Assistant, alongside Music Assistant media controls, Claude Code and OpenAI Codex agent supervision, an Aurora idle lock clock, and on-device settings.

## Language

**Growspace**:
A distinct cultivation area (such as a tent, room, or dome) monitored as a single unit with its own climate, equipment, and plants.
_Avoid_: Space

**Grow Stage**:
The current biological phase of a growspace (such as seedling, veg, flower, dry, or cure), used to determine target climate ranges and display badges.
_Avoid_: Phase, granular stage

**Overview Sensor**:
The primary Home Assistant sensor exposed by Growspace Manager for a growspace, whose state is the plant count and whose attributes hold nested environment and metrics payloads.

**Metrics Bridge**:
A flattened Home Assistant template sensor that extracts grow-stage and VPD target fields from an Overview Sensor so ESPHome can subscribe to them as top-level attributes.

**Permission Request**:
An AI coding agent (Claude Code or Codex) asking whether a specific tool call may run; the only agent moment that can be answered (Approve / Reject) from the display.
_Avoid_: Approval prompt

**Needs Input**:
The AI card state for any moment where Claude Code or Codex is waiting on the user and the display cannot answer: a multiple-choice question, a turn that ended with a question or a command for the user to run, an MCP input dialog, or an idle nudge.
_Avoid_: Question state, Waiting

**Dim State**:
The display backlight activity level (`active`, `dimmed`, or `sleeping`), driven by inactivity timeouts and woken by touch or urgent AI approval requests.

**Wake-Lock**:
A brief interaction guard engaged when waking a dimmed or sleeping display, ensuring the wake touch does not accidentally trigger underlying controls.
