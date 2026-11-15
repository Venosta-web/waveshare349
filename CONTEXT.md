# Waveshare 3.49 Growspace Display

ESPHome LVGL display and simulator for monitoring indoor cultivation environments managed by Growspace Manager in Home Assistant.

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
Claude Code asking whether a specific tool call may run; the only Claude moment that can be answered (Approve / Reject) from the display.
_Avoid_: Approval prompt

**Needs Input**:
The AI card state for any moment where Claude is waiting on the user and the display cannot answer: a multiple-choice question, a turn that ended with a question or a command for the user to run, an MCP input dialog, or an idle nudge. The detail shows what Claude asked.
_Avoid_: Question state, Waiting

**Done**:
The AI card state after a turn finished with nothing pending for the user.
_Avoid_: Ready, Finished

## Voice & Display

**Voice Satellite**:
The ESP32-S3 device running `micro_wake_word`, audio I/O, and the LVGL display.
_Avoid_: local LLM node, smart speaker

**Assist Pipeline**:
The Home Assistant-hosted STT → local LLM → TTS chain that processes voice commands and invokes tools.
_Avoid_: voice backend, AI pipeline

**Display View**:
A top-level LVGL screen selectable by touch or an exposed Home Assistant entity (such as `Growspace` or `Music`).
_Avoid_: tab, mode, screen
