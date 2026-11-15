# Claude Code bridge for the Waveshare display

Claude Code hooks → Home Assistant `input_text` helpers → the **AI** card on the
display. Tapping **APPROVE** / **REJECT** fires `esphome.claude_command` back
through Home Assistant to the waiting hook.

| Claude Code event  | Display shows                                        |
|--------------------|------------------------------------------------------|
| UserPromptSubmit   | `Working` + project and prompt                       |
| PermissionRequest  | `Pending Approval` + tool/command, amber border, buttons |
| Notification (idle)| `Needs Input`                                        |
| Stop               | `Ready`                                              |

If you don't tap within ~110 s, Home Assistant is unreachable, or anything
fails, the hook stays silent and Claude Code shows its normal prompt. It never
denies on its own. Each request carries an id, so a late tap can't approve a
different command.

## One-time setup (you)

1. **Token**: in Home Assistant, Profile → Security → Long-lived access tokens →
   Create token. Paste it after `HA_TOKEN=` in
   `~/.config/claude-ha-bridge/config.env` (mode 600, outside any repo).
2. **Let the display send events**: Settings → Devices & services → ESPHome →
   the simulator → Configure → tick *Allow the device to perform Home Assistant
   actions*.
3. **Create the helpers**:
   ```bash
   python3 ~/dev/EspHome/Waveshare349/claude_bridge/claude_ha_bridge.py setup
   ```
4. **Try it** (taps on the display print here):
   ```bash
   python3 ~/dev/EspHome/Waveshare349/claude_bridge/claude_ha_bridge.py test
   ```
5. **Install the hooks**: merge `hooks.example.json` into the `"hooks"` key of
   `~/.claude/settings.json`. They apply to every Claude Code session on this
   machine, including the desktop app.

`check` confirms the token works and lists which helpers exist.
