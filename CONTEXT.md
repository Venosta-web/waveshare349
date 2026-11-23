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
An AI coding agent (Claude Code or Codex) asking whether a specific tool call may run; the only agent moment that can be answered (Approve / Reject) from the display.
_Avoid_: Approval prompt

**Needs Input**:
The AI card state for any moment where Claude Code or Codex is waiting on the user and the display cannot answer: a multiple-choice question, a turn that ended with a question or a command for the user to run, an MCP input dialog, or an idle nudge. The detail shows what the agent asked.
_Avoid_: Question state, Waiting

**Attention State**:
Any AI card state requiring user action (**Permission Request** or **Needs Input**), which surfaces a navigation alert badge and plays an agent-specific audio chime on hardware with a speaker.

**Done**:
The AI card state after a turn finishes successfully with nothing pending for the user.
_Avoid_: Ready, Finished

**Current Activity**:
A short label on the Codex card for what the active turn is doing now, such as reading files, editing, running tests, or retrying. It can appear alongside the current and next plan steps.

**Rate Limited**:
The Codex card state when a usage limit blocks the turn from continuing. The card shows the retry time when available and changes the detail to **You can try again** once a confirmed reset time passes.

**Turn Failed**:
The Codex card state after Codex confirms a terminal error other than a blocking usage limit. The card directs the user to Codex for the error details.

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

**Feature Module**:
An optional, selectable capability in a device build (such as `Growspace`, `Music`, `Claude`, `Codex`, or `Voice Satellite`) that contributes a Display View, a hardware/pipeline capability, or both.
_Avoid_: plugin, addon

**Target Device**:
A supported hardware board profile (such as `Waveshare 3.49` or `480x320 Prototype`) that defines display geometry, pinout, and which Feature Modules are compatible.
_Avoid_: board type, hardware variant

**Setup & Build Wizard**:
The GitHub Pages web app (`site/`) where users choose a **Target Device**, **Feature Modules**, and **Presets**, customize `substitutions` and package sources, export `esphome.yaml`, and optionally flash pre-built preset firmware via USB Web Serial.
_Avoid_: feature selector, config generator

**Preset**:
A named, pre-configured combination of a **Target Device** and **Feature Modules** (in `presets/`) that CI compiles into flashable firmware binaries for the **Setup & Build Wizard**.
