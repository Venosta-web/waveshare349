---
status: accepted
---

# Composable Feature Module Packages for Multi-Device Builds

The LVGL UI previously lived in a single monolithic package (`packages/display_ui.yaml` for Waveshare 3.49 and `prototype/esp32-s3-480-320-prototype/infoscreen.yaml` for the 480x320 Prototype) with all Display Views and Home Assistant sensors tightly coupled in `switch_tab` and `home_page`. To support selecting arbitrary combinations of Feature Modules (`Growspace`, `Music`, `Claude`, `Codex`, `Voice Satellite`) across both Target Devices without duplicating YAML or stripping files with brittle text scripts, we split each device's configuration into a `board` + `core_ui` base and independent `mod_*` Feature Module packages.

Cross-module coordination uses ESPHome's `!extend` mechanism (`home_page` for adding cards and nav buttons, `switch_tab` for per-module visibility and styling) and deterministic `on_boot` priorities to reparent enabled nav buttons into `nav_rail`, hide `nav_rail` when only a single Display View is active, and select the highest-priority enabled Display View on boot.

## Considered Options

- **Keep Monolithic YAML and Strip Sections in the Wizard**: Avoids refactoring existing YAML, but makes LVGL widget and C++ `id(...)` dependency stripping fragile and hard to lint in CI.
- **Jinja2 / Custom Preprocessor Templates**: Powerful, but adds a build-time templating layer outside standard ESPHome `packages:` and prevents direct `github://` package includes in Home Assistant ESPHome Builder.
