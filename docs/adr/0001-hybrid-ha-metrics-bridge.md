---
status: accepted
---

# Hybrid HA Entities and Metrics Bridge for Growspace Data

Growspace Manager v1.3.1+ moved `environment` and `metrics` into nested dictionaries on the Overview Sensor's attributes, which ESPHome's `homeassistant` sensor platform cannot index into directly. We decided to read live climate and actuator values directly from their underlying Home Assistant entities (`sensor.*`, `fan.*`, `humidifier.*`/`switch.*`) while introducing a lightweight Home Assistant template sensor (Metrics Bridge) to flatten the computed `metrics` fields (`granular_stage`, `is_day`, `max_flower_days`/`max_veg_days`/`max_dry_days`/`max_cure_days` as `*_days`, `flower_week`, `veg_week`, `vpd_target_*`, `vpd_danger_*`). This avoids double-hopping live sensor updates through the Overview Sensor and avoids fragile C++ parsing of large nested attribute strings in ESPHome.

## Considered Options

- **Full HA Template Compatibility Layer**: Mirror all old flat attributes in HA templates; rejected because live climate/fan sensors are already first-class HA entities.
- **C++ Attribute Parsing in ESPHome**: Subscribe to raw `environment` / `metrics` attributes and parse in lambdas; rejected due to payload size and non-JSON serialization risks.
- **Upstream `growspace_manager` Changes**: Expose flat sensors directly in the custom component; deferred to keep the integration's data model decoupled from ESPHome constraints.
