# ESP32-S3 480x320 Growspace & AI Display

ESPHome LVGL display for monitoring indoor cultivation environments managed by Growspace Manager in Home Assistant, alongside Spotify media status and Claude Code agent supervision.

## Language

**Growspace**:
A distinct cultivation area (such as a tent, room, or dome) monitored as a single unit with its own climate, equipment, and plants.
_Avoid_: Space

**Grow Stage**:
The current biological phase of a growspace (such as seedling, veg, flower, dry, or cure), used to determine target climate ranges and display badges.
_Avoid_: Phase, granular stage

**Overview Sensor**:
The primary Home Assistant sensor exposed by Growspace Manager for a growspace, whose state is the plant count and whose attributes hold nested environment and metrics payloads.

**Metrics Bridge**:
A flattened Home Assistant template sensor that extracts grow-stage and VPD target fields from an Overview Sensor so ESPHome can subscribe to them as top-level attributes.

**Dim State**:
The display backlight activity level (`active`, `dimmed`, or `sleeping`), driven by inactivity timeouts and woken by touch or urgent AI approval requests.

**Wake-Lock**:
A brief interaction guard engaged when waking a dimmed or sleeping display, ensuring the wake touch does not accidentally trigger underlying controls.
