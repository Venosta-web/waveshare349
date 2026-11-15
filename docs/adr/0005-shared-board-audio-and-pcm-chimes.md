---
status: accepted
---

# Shared Board Audio Hardware and Synthesized PCM Chimes

The Waveshare 3.49 board includes an ES8311 DAC and speaker over I2S, which previously lived inside `packages/waveshare_349/mod_voice.yaml` (`Voice Satellite`). AI Desk and Grow+AI builds without `Voice Satellite` still need to play attention chimes when Claude Code or Codex enters an Attention State (`Permission Request` or `Needs Input`), while builds *with* `Voice Satellite` must share the I2S bus and avoid colliding with active voice sessions.

We moved `i2s_audio`, `audio_dac` (`es8311_dac`), and `speaker` (`spk_i2s`) into `packages/waveshare_349/board.yaml`, leaving `microphone`, `micro_wake_word`, and `voice_assistant` in `mod_voice.yaml`. A shared `voice_active` global in `core_ui.yaml` lets `mod_claude` and `mod_codex` skip chimes during an active voice interaction without referencing `id(va)` directly (which does not exist when `mod_voice` is omitted). Chimes are synthesized at runtime in `packages/waveshare_349/ai_sounds.h` as 16 kHz 16-bit mono PCM sine/harmonic sequences with smooth envelopes, giving distinct motifs per agent and Attention State with zero external audio assets.

## Considered Options

- **Keep `speaker` inside `mod_voice.yaml`**: Simpler, but disables AI sound effects whenever `Voice Satellite` is not included in a build.
- **Bundled WAV/MP3 assets**: Richer audio, but adds binary asset management, larger flash usage, and extra `media_player` pipeline overhead compared to short synthesized chimes.
