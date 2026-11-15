# Waveshare 3.49 Growspace Display

ESPHome LVGL firmware and SDL2 desktop simulator for the **Waveshare ESP32-S3-Touch-LCD-3.49** (640×172 landscape). Built for monitoring indoor cultivation environments managed by [Growspace Manager](https://github.com/holt-dev/growspace_manager) in Home Assistant, paired with a local **Voice Satellite**, **Music Assistant** controller, and an interactive **Claude Code** status & approval card.

---

## Features

### 1. Growspace Monitor (`Growspace` Display View)
- **Auto-cycling multi-growspace dashboard** (7 growspaces configured by default: `4x4`, `SOG 2x5`, `Nursery Dome`, `Clone`, `Mother`, `Dry`, `Cure`).
- **Live climate & VPD gauge**: Temperature, humidity, and calculated VPD plotted against configurable `vpd_gauge_min` (`0.4`) / `vpd_gauge_max` (`1.8`) and stage-specific target/danger bands.
- **Grow Stage & equipment status**: Displays current Grow Stage, day/night state, veg/flower week, and live exhaust, circulation, and humidifier states.
- **Hybrid Home Assistant integration**: Subscribes directly to live climate/actuator entities while using a lightweight **Metrics Bridge** template sensor to flatten nested Growspace Manager v1.3.1+ `metrics` attributes (see [ADR-0001](docs/adr/0001-hybrid-ha-metrics-bridge.md)).

### 2. Music Assistant Controller (`Music` Display View)
- Binds to any Home Assistant `media_player` entity (`music_player_entity`).
- Touch controls for play/pause, previous/next track, shuffle, repeat (`off` / `all` / `one`), volume, and mute.
- Two selectable LVGL card variants via `music_variant`:
  - **`studio`** *(default)* — EQ & 12-band 10 Hz spectrum meter console ([`music/studio.yaml`](music/studio.yaml)).
  - **`deck`** — Animated vinyl turntable with 10 Hz strobe ([`music/deck.yaml`](music/deck.yaml)).

### 3. Claude Code AI Bridge (`AI` Display View)
- Connects local Claude Code sessions (CLI and Desktop) to the display via [`claude_bridge/claude_ha_bridge.py`](claude_bridge/claude_ha_bridge.py) and Home Assistant `input_text` helpers.
- Surfaces live session states: `Working`, `Pending Approval`, `Needs Input`, `Done`, and `Rate Limited` (with 5h/7d countdown bar and reset time).
- **Touchscreen Permission Requests**: When Claude Code requests tool/command permission, the display switches to the `AI` view and lets you tap **APPROVE** or **REJECT** (fires `esphome.claude_command` back to the waiting hook; silently falls back to the normal Claude prompt after `claude_decision_timeout`, default `110s`).
- Two selectable LVGL card variants via `claude_variant`:
  - **`stage`** *(default on hardware)* — Mascot-forward animated Clawd puppet ([`claude/stage.yaml`](claude/stage.yaml), [`claude/clawd.h`](claude/clawd.h)).
  - **`terminal`** *(default on simulator)* — Compact CLI status line ([`claude/terminal.yaml`](claude/terminal.yaml)).

### 4. Voice Satellite & Local Assist Pipeline
- **On-device wake word & audio** ([`waveshare_349.yaml`](waveshare_349.yaml)): `micro_wake_word` (`okay_nabu`), ES8311 I2S audio codec, noise suppression, auto-gain, push-to-talk (`on_voice_ptt`), and a `Mute Microphone` switch (`mic_mute_switch`).
- **100% local server pipeline** (see [ADR-0002](docs/adr/0002-voice-satellite-ha-local-llm.md) & [`ha_assist_tools.yaml`](ha_assist_tools.yaml)): Designed for Home Assistant Assist with Wyoming `faster-whisper` (STT), Ollama tool-calling LLM (e.g., `qwen2.5:7b` / `qwen3:8b`), and Wyoming `piper` (TTS).
- **Declarative Display View control**: Exposes `select.<device>_display_view` (`Growspace`, `Music`, `AI`) so voice commands and Automations can switch screens directly.

---

## Hardware & Pinout

Target board: **Waveshare ESP32-S3-Touch-LCD-3.49** (`esp32-s3-devkitc-1`, ESP-IDF, 240 MHz, 80 MHz Octal PSRAM).

| Subsystem | Interface / Chip | Pins |
|---|---|---|
| Display | QSPI (`WAVESHARE-ESP32-S3-TOUCH-LCD-3.49`, 172×640 rotated 90°) | CLK `GPIO10`, D0–D3 `GPIO11`–`GPIO14`, CS `GPIO9`, RST `GPIO21` |
| Touchscreen | I²C (`axs15231`) | SDA `GPIO17`, SCL `GPIO18` |
| System I²C | I²C (`sys_i2c`) | SDA `GPIO47`, SCL `GPIO48` |
| Audio Codec & I²S | `es8311` + I²S Mic & Speaker (16-bit, 16 kHz) | LRCLK `GPIO46`, BCLK `GPIO15`, MCLK `GPIO7`, DIN `GPIO45`, DOUT `GPIO6` |

---

## Getting Started

### Prerequisites
- [ESPHome](https://esphome.io/) (`>= 2025.11.0`)
- [Home Assistant](https://www.home-assistant.io/) with [Growspace Manager](https://github.com/holt-dev/growspace_manager) (v1.3.1+)
- *(Optional, for desktop simulator)* SDL2 development headers (`libsdl2-dev` on Debian/Ubuntu)
- *(Optional, for Claude Code bridge)* Python 3 with `websockets` / `requests` and `~/.claude/settings.json` hooks

### 1. Home Assistant Setup

1. **Metrics Bridge** — Include [`ha_metrics_bridge.yaml`](ha_metrics_bridge.yaml) in your Home Assistant `configuration.yaml` (e.g., via `packages:`) and update the source `sensor.<growspace>` entity IDs to match your Overview Sensors.
2. **Assist Tools *(optional)*** — Include [`ha_assist_tools.yaml`](ha_assist_tools.yaml) in your Home Assistant scripts and expose the following to Assist under **Settings → Voice assistants → Expose**:
   - `select.waveshare_349_voice_display_view`
   - `media_player.music_assistant_player` (or your configured player)
   - `script.switch_display_view`
   - `script.open_music_view_and_play`

### 2. Configure & Flash the Device

1. Edit the `substitutions:` block at the top of [`waveshare_349.yaml`](waveshare_349.yaml) (or override via CLI `-s`):
   - Set `wifi_ssid` and `wifi_password`
   - Adjust `gs_count` and `gs_0_*` … `gs_6_*` entity IDs
   - Set `music_player_entity`, `music_variant` (`studio` | `deck`), and `claude_variant` (`stage` | `terminal`)
2. Compile and upload via ESPHome:
   ```bash
   esphome run waveshare_349.yaml
   ```
3. Add the discovered ESPHome device in Home Assistant (**Settings → Devices & services → ESPHome**). If using the Claude Code bridge, open **Configure** on the device integration and enable **Allow the device to perform Home Assistant actions**.

### 3. Run the Desktop SDL2 Simulator

Test the full LVGL UI and Home Assistant connection on Linux/WSL/macOS without flashing hardware:

```bash
# Install SDL2 (Debian/Ubuntu)
sudo apt-get install -y libsdl2-dev

# Run the 640x172 SDL2 simulator (API listens on TCP 6053)
esphome run waveshare_sim.yaml
```

- Press **`F12`** inside the simulator window to save a PNG screenshot.
- Override card variants on the fly:
  ```bash
  esphome run waveshare_sim.yaml -s claude_variant stage -s music_variant deck
  ```

### 4. Set Up the Claude Code Bridge *(optional)*

See [`claude_bridge/README.md`](claude_bridge/README.md) for full details. Quick summary:

1. Create a Home Assistant Long-Lived Access Token and save `HA_TOKEN=...` in `~/.config/claude-ha-bridge/config.env` (`chmod 600`).
2. Create the required `input_text` helpers in Home Assistant:
   ```bash
   python3 claude_bridge/claude_ha_bridge.py setup
   ```
3. Verify the connection and test display taps:
   ```bash
   python3 claude_bridge/claude_ha_bridge.py check
   python3 claude_bridge/claude_ha_bridge.py test
   ```
4. Merge [`claude_bridge/hooks.example.json`](claude_bridge/hooks.example.json) into the `"hooks"` key of `~/.claude/settings.json`.

### 5. Set Up the Codex Desktop Bridge *(optional)*

The **Codex** Display View shows local Codex turn status, plan steps, and optional touchscreen decisions for eligible permission requests. See the [Codex bridge setup](codex_bridge/README.md) for Home Assistant helper setup, the desktop hooks, and the touchscreen test.

---

## Configuration Reference

Key `substitutions` shared across [`waveshare_349.yaml`](waveshare_349.yaml) and [`waveshare_sim.yaml`](waveshare_sim.yaml):

| Substitution | Default (HW / Sim) | Description |
|---|---|---|
| `gs_count` | `"7"` | Number of growspaces to cycle through |
| `cycle_interval` | `"15"` | Seconds between automatic growspace rotations |
| `pause_timeout` | `"25"` | Seconds to pause auto-cycle after manual interaction |
| `vpd_gauge_min` / `vpd_gauge_max` | `"0.4"` / `"1.8"` | VPD gauge scale bounds (kPa) |
| `music_variant` | `"studio"` | Music card layout: `"studio"` or `"deck"` |
| `music_player_entity` | `"media_player.music_assistant_player"` | Home Assistant media player entity ID |
| `claude_variant` | `"stage"` / `"terminal"` | AI card layout: `"stage"` or `"terminal"` |
| `claude_decision_timeout` | `"110"` | Seconds the bridge waits for an Approve/Reject tap |

---

## Headless Previews & GIF Generation

Record automated state walkthroughs for the AI and Music views without a physical screen:

```bash
# AI card preview (outputs to out/)
ESPHOME_SNAPSHOT_DIR=out esphome run preview.yaml
ESPHOME_SNAPSHOT_DIR=out esphome run preview.yaml -s claude_variant terminal

# Music card preview (outputs to out/)
ESPHOME_SNAPSHOT_DIR=out esphome run music_preview.yaml -s music_variant studio
ESPHOME_SNAPSHOT_DIR=out esphome run music_preview.yaml -s music_variant deck
```

---

## Repository Structure

```text
.
├── waveshare_349.yaml          # Hardware firmware config (ESP32-S3 + ES8311 + LVGL + Wake Word)
├── waveshare_sim.yaml          # Desktop SDL2 simulator config (640x172)
├── preview.yaml                # Headless snapshot runner for AI card states
├── music_preview.yaml          # Headless snapshot runner for Music card states
├── ha_metrics_bridge.yaml      # Home Assistant template sensors (Metrics Bridge)
├── ha_assist_tools.yaml        # Home Assistant Assist scripts for local LLM control
├── packages/
│   └── display_ui.yaml         # Shared LVGL UI, Growspace view, navigation, & Display View select
├── claude/
│   ├── clawd.h                 # Clawd puppet sprite/animation header
│   ├── stage.yaml / stage_card.yaml
│   └── terminal.yaml / terminal_card.yaml
├── claude_bridge/
│   ├── claude_ha_bridge.py     # Claude Code <-> Home Assistant hook bridge
│   ├── test_claude_ha_bridge.py
│   ├── hooks.example.json
│   └── README.md
├── music/
│   ├── common.yaml             # Shared Music Assistant media_player bindings
│   ├── deck.yaml / deck_card.yaml
│   └── studio.yaml / studio_card.yaml
├── fonts/                      # Bundled DejaVu & Material Design Icons fonts
├── docs/adr/                   # Architecture Decision Records
├── CONTEXT.md                  # Domain glossary and terminology
└── .github/workflows/
    └── esphome-lint.yml        # CI validation with `esphome config`
```

---

## Validation & Testing

- **ESPHome config validation** (mirrors [`.github/workflows/esphome-lint.yml`](.github/workflows/esphome-lint.yml)):
  ```bash
  for f in *.yaml; do
    grep -Eq '^(esphome|packages):' "$f" && esphome config "$f"
  done
  ```
- **Claude bridge unit tests**:
  ```bash
  python3 -m unittest claude_bridge/test_claude_ha_bridge.py
  ```

---

## License

Licensed under the **GNU General Public License v3.0** — see [`LICENSE`](LICENSE) for details.
