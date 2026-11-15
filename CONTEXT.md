# Waveshare 3.49 Growspace Display

ESPHome LVGL display and simulator for monitoring indoor cultivation environments managed by Growspace Manager in Home Assistant.

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
