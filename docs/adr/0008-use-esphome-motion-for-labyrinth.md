---
status: accepted
---

# Use ESPHome Motion for Labyrinth

Labyrinth uses ESPHome’s native QMI8658 driver to share the Waveshare 3.49 system I²C bus and obtain steady-tilt input. We chose the upstream driver over maintaining register access in this repository, accepting ESPHome 2026.7.0 as the minimum for builds that include the optional Labyrinth module. The board package retains its existing minimum so users can still build configurations without the game on their current toolchain.
