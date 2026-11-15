# Claude Code bridge for the Waveshare display

Claude Code hooks → Home Assistant `input_text` helpers → the **AI** card on the
display. Tapping **APPROVE** / **REJECT** fires `esphome.claude_command` back
through Home Assistant to the waiting hook.

| Claude Code event                         | Display shows                                            |
|-------------------------------------------|----------------------------------------------------------|
| UserPromptSubmit                          | `Working` + project and prompt                           |
| PermissionRequest                         | `Pending Approval` + tool/command, amber border, buttons |
| PreToolUse `AskUserQuestion`              | `Needs Input` + the question and its options             |
| PostToolUse `AskUserQuestion`             | `Working`                                                |
| Notification `elicitation_dialog`         | `Needs Input` + what the MCP server asks                 |
| Notification `idle_prompt`                | `Needs Input`                                            |
| Stop, turn ends with a question           | `Needs Input` + the question                             |
| Stop, turn hands you a command to run     | `Needs Input` + `Run: <command>`                         |
| Stop, otherwise                           | `Done`                                                   |
| StopFailure `rate_limit`                  | `Rate Limited` + 5h/7d countdown bar and reset time      |

At Stop the bridge reads the turn's transcript: the desktop app's end-of-turn
question box is used when there is one; otherwise it guesses from the last
message (last paragraph ends in `?`, or a shell code block).

A `Needs Input` belongs to the session that raised it. Other sessions'
`Working` / `Done` don't hide it until that session moves on (new prompt,
answered question, or 12 h). The owner is kept in
`~/.cache/claude-ha-bridge/needs_input_owner.json`.

If you don't tap within ~110 s, Home Assistant is unreachable, or anything
fails, the hook stays silent and Claude Code shows its normal prompt. It never
denies on its own. Each request carries an id, so a late tap can't approve a
different command.

## One-time setup (you)

1. **Token**: in Home Assistant, Profile → Security → Long-lived access tokens →
   Create token. Paste it after `HA_TOKEN=` in
   `~/.config/claude-ha-bridge/config.env` (mode 600, outside any repo).
2. **Let each display send events**: Settings → Devices & services → ESPHome →
   each display (simulator, Waveshare 3.49, 480x320, etc.) → Configure → tick
   *Allow the device to perform Home Assistant actions*.
3. **Create the helpers**:
   ```bash
   python3 ~/dev/EspHome/Waveshare349/claude_bridge/claude_ha_bridge.py setup
   ```
4. **Try it** (taps on any display print here):
   ```bash
   python3 ~/dev/EspHome/Waveshare349/claude_bridge/claude_ha_bridge.py test
   ```
5. **Install the hooks**: merge `hooks.example.json` into the `"hooks"` key of
   `~/.claude/settings.json`. They apply to every Claude Code session on this
   machine, including the desktop app.

`check` confirms the token works and lists which helpers exist.

## Multiple displays (Shared Mirror)

All connected ESPHome displays subscribe to the same `input_text.claude_*`
helpers and mirror the active Claude Code feed simultaneously:

- Tapping **APPROVE** / **REJECT** on any display fires `esphome.claude_command`,
  and the bridge clears `input_text.claude_request` so buttons dismiss on all
  other displays as well.
- Make sure every physical/simulator display is paired in Home Assistant's
  ESPHome integration and has *Allow the device to perform Home Assistant
  actions* enabled.
