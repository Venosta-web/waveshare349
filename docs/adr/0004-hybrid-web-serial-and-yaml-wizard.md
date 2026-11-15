---
status: accepted
---

# Hybrid Web Serial Presets and Custom YAML Setup Wizard

GitHub Pages is a static host and cannot run an ESPHome C++ compiler on demand, while ESPHome `homeassistant` sensor `entity_id:` bindings are resolved at compile time via `substitutions:`. We chose a hybrid zero-build static wizard (`site/`) that pairs **ESP Web Tools** (Web Serial) for curated pre-built firmware presets with a **Custom YAML Generator** for full entity, growspace-count, and card-variant customization.

Pre-built presets are compiled in GitHub Actions with `improv_serial:` and `captive_portal:` enabled (no hardcoded Wi-Fi credentials) and use the repository's standard Home Assistant bridge entity names. The Custom YAML Generator defaults to `github://` remote `packages:` so Home Assistant ESPHome Builder users can paste a single file without cloning the repository, with a toggle for local `packages/` paths.

## Considered Options

- **Full Combinatorial CI Matrix**: Compiling every `(Target Device × Feature Module × Variant)` permutation in GitHub Actions would require 40+ ESPHome builds per release and still wouldn't allow custom HA entity IDs without recompiling.
- **Runtime Entity Configuration via HA Text Entities**: Moving 50+ growspace and bridge entity IDs into runtime `text` entities would allow a single binary to be reconfigured, but requires rewriting ESPHome's `homeassistant` sensor bindings and adds significant flash/RAM overhead.
- **YAML Generator Only (No Web Serial)**: Simpler CI, but removes one-click USB flashing for users who follow the standard bridge conventions.
