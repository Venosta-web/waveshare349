# Codex Desktop display bridge

The bridge runs on the Linux desktop that hosts Codex. It uses [Codex lifecycle hooks](https://learn.chatgpt.com/docs/hooks) to update the **Codex** Display View through Home Assistant. There is no background service. User-level hooks also apply to local Codex CLI sessions.

## Set up Home Assistant

1. Use Python 3.10+ with `websockets>=15`. Run the commands below with the same Python interpreter you specify in the hook configuration.
2. Set `HA_URL` and `HA_TOKEN` as environment variables or in `~/.config/codex-ha-bridge/config.env` (permissions `600`). The bridge can also reuse `~/.config/claude-ha-bridge/config.env`. Environment variables take precedence, then the Codex file, then the Claude file. The URL defaults to `http://homeassistant.local:8123`.
3. Run the following from the repository root. Creating helpers requires a Home Assistant administrator token. Existing helpers are verified in place.

   ```bash
   python3 codex_bridge/codex_ha_bridge.py setup
   python3 codex_bridge/codex_ha_bridge.py check
   ```

   If you manage helpers in YAML or through the Home Assistant UI, use these exact entity IDs with a minimum length of 0.

   | Entity | Max length |
   | --- | ---: |
   | `input_text.codex_status` | 100 |
   | `input_text.codex_detail` | 255 |
   | `input_text.codex_request` | 64 |
   | `input_text.codex_steps` | 255 |

4. Select **Codex** on the display. To check touchscreen events, open the ESPHome device's integration options in Home Assistant and enable **Allow the device to perform Home Assistant actions**. With Codex idle, run `python3 codex_bridge/codex_ha_bridge.py test` and tap **APPROVE** or **REJECT**. This sends a synthetic request and prints the tap it received.

## Connect Codex Desktop

1. Copy `codex_ha_bridge.py` to a stable location outside directories your Codex chats can edit, such as `~/.local/share/codex-ha-bridge/`.
2. Replace the script path and, if needed, the Python interpreter in [hooks.example.json](hooks.example.json). Merge its handler entries into `~/.codex/hooks.json`, or add equivalent handlers to the existing inline hook tables in `~/.codex/config.toml`. Preserve existing handlers.
3. In Codex Desktop, open **Settings → Hooks** or choose **Review hooks** in the composer. Review and trust the bridge hooks. Then send a prompt and check for **Working** followed by **Done** on the display.

The latest submitted turn owns the display. It starts with empty request and step helpers. An `update_plan` hook publishes the current and next steps. Tool requests that need a native answer can show **Needs Input** when Codex emits the corresponding hook. A finished turn shows **Done**, and interrupting or ending the displayed session clears the status. The bridge publishes the project directory name and plan labels. Prompt and reply bodies stay local. Quota countdowns require structured reset data that the installed hooks do not provide.

## Enable touchscreen decisions

The sample uses passive status. To grant the display authority to answer supported Codex permission requests, append `--touch-approvals` to the `PermissionRequest` handler's command and review the changed hook in Codex.

Eligible requests are short, single-line ASCII Bash commands with a command string in the hook payload. The full command is sent to Home Assistant for display. The hook subscribes to `esphome.codex_command` before publishing **Pending Approval** with a one-time request ID. It accepts one matching tap and returns the Codex `allow` or `deny` decision. The native approval flow handles unsupported requests and unanswered requests after 45 seconds. Only use this mode if you trust the display and Home Assistant users or automations that can read the helpers and fire events.

## Run the tests

```bash
python3 -m unittest discover -s codex_bridge
```
