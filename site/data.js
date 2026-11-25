export const TARGET_DEVICES = [
  {
    id: 'waveshare-349',
    name: 'Waveshare 3.49"',
    subtitle: 'ESP32-S3-Touch-LCD-3.49',
    resolution: '640 × 172',
    chip: 'ESP32-S3 + Octal PSRAM + ES8311 Audio',
    packageDir: 'packages/waveshare_349',
    defaultDeviceName: 'waveshare-349-voice',
    defaultFriendlyName: 'Waveshare 3.49 Display',
    supportedModules: ['grow', 'music', 'claude', 'codex', 'voice', 'idle', 'labyrinth', 'settings'],
    unsupportedReasons: {},
  },
  {
    id: 'prototype-480x320',
    name: '480×320 Prototype',
    subtitle: 'ESP32-S3 QSPI AXS15231',
    resolution: '480 × 320',
    chip: 'ESP32-S3 16MB + Octal PSRAM + Backlight PWM',
    packageDir: 'packages/prototype_480x320',
    defaultDeviceName: 'infoscreen-480x320',
    defaultFriendlyName: '480x320 Prototype Display',
    supportedModules: ['grow', 'music', 'claude', 'codex', 'idle', 'settings'],
    unsupportedReasons: {
      voice: 'No onboard I2S mic/speaker codec on this board.',
      labyrinth: 'Requires the Waveshare 3.49 motion sensor.',
    },
  },
];

export const FEATURE_MODULES = [
  {
    id: 'grow',
    name: 'Growspace',
    kind: 'Display View',
    description: 'VPD gauge, target range, temp/humidity, equipment status, and auto-cycling across 1–7 growspaces.',
    defaultEnabled: true,
  },
  {
    id: 'music',
    name: 'Music Assistant',
    kind: 'Display View',
    description: 'Active media player controls, track info, shuffle/repeat, and volume in Studio or Vinyl Deck layout.',
    defaultEnabled: true,
  },
  {
    id: 'claude',
    name: 'Claude Code',
    kind: 'Display View',
    description: 'Live agent status, step tracker, rate-limit countdown, and touch Approve/Reject for Permission Requests.',
    defaultEnabled: true,
  },
  {
    id: 'codex',
    name: 'Codex',
    kind: 'Display View',
    description: 'OpenAI Codex CLI status, step tracker, and touch Approve/Reject for Permission Requests.',
    defaultEnabled: true,
  },
  {
    id: 'voice',
    name: 'Voice Satellite',
    kind: 'Assist Pipeline',
    description: 'On-device "Okay Nabu" micro_wake_word, ES8311 I2S audio, push-to-talk button, and live voice overlay.',
    defaultEnabled: true,
  },
  {
    id: 'idle',
    name: 'Idle Lock Screen',
    kind: 'Lock Overlay',
    description: 'Aurora ambient nightstand clock and status overlay with configurable inactivity timeout and manual lock.',
    defaultEnabled: true,
  },
  {
    id: 'labyrinth',
    name: 'Labyrinth',
    kind: 'Display View',
    description: 'Tilt the display to roll a marble into the marked goal hole. Requires ESPHome 2026.7.0 or later.',
    defaultEnabled: true,
  },
  {
    id: 'settings',
    name: 'Device Settings',
    kind: 'Display View',
    description: 'On-device Display (brightness, lock timeout), Growspace (auto-cycle, speed), and System (telemetry, safe restart) menu.',
    defaultEnabled: true,
  },
];

export const PRESETS = [
  {
    id: 'waveshare-349-full-suite',
    deviceId: 'waveshare-349',
    name: 'Full Suite',
    modules: ['grow', 'music', 'claude', 'codex', 'voice', 'idle', 'labyrinth', 'settings'],
    defaults: { musicVariant: 'studio', claudeVariant: 'terminal', codexVariant: 'terminal' },
  },
  {
    id: 'waveshare-349-grow-voice',
    deviceId: 'waveshare-349',
    name: 'Grow + Voice',
    modules: ['grow', 'voice'],
    defaults: {},
  },
  {
    id: 'waveshare-349-grow-music',
    deviceId: 'waveshare-349',
    name: 'Grow + Music',
    modules: ['grow', 'music'],
    defaults: { musicVariant: 'studio' },
  },
  {
    id: 'waveshare-349-ai-desk',
    deviceId: 'waveshare-349',
    name: 'AI Desk Companion',
    modules: ['claude', 'codex', 'voice'],
    defaults: { claudeVariant: 'terminal', codexVariant: 'terminal' },
  },
  {
    id: 'waveshare-349-grow-only',
    deviceId: 'waveshare-349',
    name: 'Grow Only',
    modules: ['grow'],
    defaults: {},
  },
  {
    id: 'prototype-480x320-full',
    deviceId: 'prototype-480x320',
    name: 'Full Prototype',
    modules: ['grow', 'music', 'claude', 'codex', 'idle', 'settings'],
    defaults: { musicVariant: 'studio', claudeVariant: 'terminal', codexVariant: 'terminal' },
  },
  {
    id: 'prototype-480x320-ai-desk',
    deviceId: 'prototype-480x320',
    name: 'AI Desk Companion',
    modules: ['claude', 'codex', 'idle', 'settings'],
    defaults: { claudeVariant: 'terminal', codexVariant: 'terminal' },
  },
  {
    id: 'prototype-480x320-grow-music',
    deviceId: 'prototype-480x320',
    name: 'Grow + Music',
    modules: ['grow', 'music'],
    defaults: { musicVariant: 'studio' },
  },
  {
    id: 'prototype-480x320-grow-only',
    deviceId: 'prototype-480x320',
    name: 'Grow Only',
    modules: ['grow'],
    defaults: {},
  },
];

export const DEFAULT_GROWSPACES = [
  { name: '4x4', slug: '4x4' },
  { name: 'SOG 2x5', slug: 'sog_2x5' },
  { name: 'Nursery Dome', slug: 'nursery_dome' },
  { name: 'Clone', slug: 'clone' },
  { name: 'Mother', slug: 'mother' },
  { name: 'Dry', slug: 'dry' },
  { name: 'Cure', slug: 'cure' },
];

export const HA_COMPANION_GUIDES = {
  grow: {
    title: 'Growspace Manager + Metrics Bridge',
    file: 'ha_metrics_bridge.yaml',
    summary: 'Flattens Growspace Manager v1.3.1+ nested attributes (stage, VPD targets, danger bounds) into top-level template sensor attributes.',
    steps: [
      'Install Growspace Manager (v1.3.1+) in Home Assistant.',
      'Copy ha_metrics_bridge.yaml into your Home Assistant packages/ or template: configuration.',
      'Ensure each active growspace has its *_metrics_bridge, *_calculated_vpd, *_temperature, and *_humidity entities.',
    ],
  },
  music: {
    title: 'Music Assistant Bridge',
    file: 'ha_music_bridge.yaml',
    summary: 'Auto-detects the active Music Assistant media_player and remembers the last player used.',
    steps: [
      'Copy ha_music_bridge.yaml into Home Assistant.',
      'Set music_player_entity to sensor.music_assistant_bridge.',
    ],
  },
  claude: {
    title: 'Claude Code Hook & HA Bridge',
    file: 'claude_bridge/',
    summary: 'Pushes Claude Code session status, steps, rate limits, and Permission Requests to input_text.claude_* helpers.',
    steps: [
      'Create input_text.claude_status, input_text.claude_detail, input_text.claude_request, and input_text.claude_steps in Home Assistant.',
      'Configure claude_bridge/claude_ha_bridge.py in your Claude Code hooks.',
    ],
  },
  codex: {
    title: 'Codex Hook & HA Bridge',
    file: 'codex_bridge/',
    summary: 'Pushes Codex CLI status, steps, and Permission Requests to input_text.codex_* helpers.',
    steps: [
      'Create input_text.codex_status, input_text.codex_detail, input_text.codex_request, and input_text.codex_steps in Home Assistant.',
      'Run the bridge script from codex_bridge/ alongside your Codex CLI session.',
    ],
  },
  voice: {
    title: 'Home Assistant Assist Pipeline',
    file: 'ha_assist_tools.yaml',
    summary: 'Connects the Voice Satellite to your Home Assistant STT → local LLM → TTS Assist Pipeline and Display View switching.',
    steps: [
      'Adopt the device in Home Assistant → Settings → Devices & Services → ESPHome.',
      'Assign an Assist Pipeline on the ESPHome device page and optionally import ha_assist_tools.yaml.',
    ],
  },
};
