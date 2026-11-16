---
status: accepted
---

# Observe Codex Turns Through Local Transcripts

The Codex bridge's hooks report plan updates and user requests, while local Desktop transcripts also record tool activity and terminal usage-limit errors. Keep the hooks and follow the transcript of the latest submitted turn so the display can show those events in the user's existing Desktop session. App Server exposes structured events, but a way to attach the bridge to the existing Desktop session has yet to be verified.

The transcript format can change across Codex versions. Read known event fields, fall back to hook-driven status when transcript data is unavailable, and discard events from turns that no longer own the display. Publish short activity labels, plan labels, and any extracted retry time to Home Assistant. Keep transcript bodies, command arguments, and tool output local.
