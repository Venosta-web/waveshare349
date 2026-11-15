# Voice Satellite + Home Assistant Local LLM

Status: accepted

Because the ESP32-S3 lacks the RAM and compute to run a general-purpose LLM on-chip (even 4-bit quantized models require ~80–300+ MB RAM vs. 8–16 MB PSRAM), we split the architecture into an ESPHome **Voice Satellite** and a Home Assistant **Assist Pipeline**.

The Waveshare 3.49 ESP32-S3 handles `micro_wake_word`, ES8311 I2S audio I/O, and LVGL rendering, while Home Assistant runs local Wyoming Whisper (STT), Ollama (local tool-calling LLM), and Wyoming Piper (TTS) on the home server. Display control is exposed declaratively via ESPHome entities (such as a `Display View` `select` entity) so the LLM controls the screen using standard Home Assistant tool calls alongside smart-home actions like `media_player`.

## Considered Options

- **On-device LLM on ESP32-S3**: Rejected; physically impossible within ESP32-S3 SRAM/PSRAM limits.
- **On-device fixed-phrase spotting only**: Rejected as primary path; cannot handle open-ended Home Assistant queries or compound commands without a server LLM.
- **Device-side intent parsing**: Rejected; keeps reasoning in the Home Assistant LLM/tool layer and keeps ESPHome firmware simple and deterministic.
