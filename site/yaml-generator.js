import { DEFAULT_GROWSPACES, TARGET_DEVICES } from './data.js';

function quoteYaml(val) {
  return `"${String(val ?? '').replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`;
}

function slugify(name) {
  return (
    name
      .toLowerCase()
      .trim()
      .replace(/[^a-z0-9]+/g, '_')
      .replace(/^_+|_+$/g, '') || 'growspace'
  );
}

export function generateEsphomeYaml(state) {
  const device = TARGET_DEVICES.find((d) => d.id === state.deviceId) || TARGET_DEVICES[0];
  const mods = state.modules;
  const has = (id) => mods.includes(id);

  const lines = [];
  lines.push('substitutions:');
  lines.push(`  device_name: ${quoteYaml(state.deviceName || device.defaultDeviceName)}`);
  lines.push(`  friendly_name: ${quoteYaml(state.friendlyName || device.defaultFriendlyName)}`);

  if (!state.useImprov && (state.wifiSsid || state.wifiPassword)) {
    lines.push(`  wifi_ssid: ${quoteYaml(state.wifiSsid || '')}`);
    lines.push(`  wifi_password: ${quoteYaml(state.wifiPassword || '')}`);
  }

  if (has('grow')) {
    lines.push('');
    lines.push('  # Growspace Configuration');
    lines.push(`  gs_count: ${quoteYaml(state.gsCount)}`);
    lines.push(`  cycle_interval: ${quoteYaml(state.cycleInterval)}`);
    lines.push(`  pause_timeout: ${quoteYaml(state.pauseTimeout)}`);
    lines.push('  vpd_gauge_min: "0.4"');
    lines.push('  vpd_gauge_max: "1.8"');

    // Always emit all 7 slots so ESPHome sensor bindings in mod_grow.yaml resolve cleanly
    for (let i = 0; i < 7; i++) {
      const custom = state.growspaces[i] || DEFAULT_GROWSPACES[i];
      const name = custom.name || DEFAULT_GROWSPACES[i].name;
      const slug = custom.slug || slugify(name);
      lines.push('');
      lines.push(`  # Space ${i}: ${name}`);
      lines.push(`  gs_${i}_name: ${quoteYaml(name)}`);
      lines.push(`  gs_${i}_metrics: ${quoteYaml(`sensor.${slug}_metrics_bridge`)}`);
      lines.push(`  gs_${i}_vpd: ${quoteYaml(`sensor.${slug}_calculated_vpd`)}`);
      lines.push(`  gs_${i}_temp: ${quoteYaml(`sensor.${slug}_temperature`)}`);
      lines.push(`  gs_${i}_hum: ${quoteYaml(`sensor.${slug}_humidity`)}`);
      lines.push(`  gs_${i}_exhaust: ${quoteYaml(`fan.${slug}_exhaust`)}`);
      lines.push(`  gs_${i}_circ: ${quoteYaml(`fan.${slug}_circulation`)}`);
      lines.push(`  gs_${i}_humid: ${quoteYaml(`humidifier.${slug}_humidifier`)}`);
    }
  }

  if (device.id === 'prototype-480x320') {
    lines.push('');
    lines.push('  # 480x320 Prototype Power & Hardware');
    lines.push('  dim_timeout: "5"');
    lines.push('  sleep_timeout: "15"');
    lines.push(`  battery_entity: ${quoteYaml(state.batteryEntity || 'sensor.m2101k7bny_battery_level')}`);
  }

  if (has('music')) {
    lines.push('');
    lines.push('  # Music Assistant');
    lines.push(`  music_variant: ${quoteYaml(state.musicVariant)}`);
    lines.push(`  music_player_entity: ${quoteYaml(state.musicPlayerEntity)}`);
  }

  if (has('claude')) {
    lines.push('');
    lines.push('  # Claude Code');
    lines.push(`  claude_variant: ${quoteYaml(state.claudeVariant)}`);
    lines.push('  claude_decision_timeout: "110"');
  }

  if (has('codex')) {
    lines.push('');
    lines.push('  # Codex');
    lines.push(`  codex_variant: ${quoteYaml(state.codexVariant)}`);
    lines.push('  codex_decision_timeout: "110"');
  }

  if (has('idle')) {
    lines.push('');
    lines.push('  # Idle / Lock Clock Screen');
    lines.push('  idle_variant: "aurora"');
    lines.push(`  idle_timeout: ${quoteYaml(state.idleTimeout || 300)}`);
  }

  lines.push('');
  lines.push('packages:');

  const wifiPkg = state.useImprov
    ? 'packages/common/wifi_improv.yaml'
    : state.wifiSsid || state.wifiPassword
      ? 'packages/common/wifi_static.yaml'
      : 'packages/common/wifi_secrets.yaml';
  const pkgFiles = [
    ['board', `${device.packageDir}/board.yaml`],
    ['wifi', wifiPkg],
    ['core_ui', `${device.packageDir}/core_ui.yaml`],
  ];

  if (has('grow')) pkgFiles.push(['mod_grow', `${device.packageDir}/mod_grow.yaml`]);
  if (has('music')) pkgFiles.push(['mod_music', `${device.packageDir}/mod_music.yaml`]);
  if (has('claude')) pkgFiles.push(['mod_claude', `${device.packageDir}/mod_claude.yaml`]);
  if (has('codex')) pkgFiles.push(['mod_codex', `${device.packageDir}/mod_codex.yaml`]);
  if (has('voice') && device.id === 'waveshare-349') {
    pkgFiles.push(['mod_voice', `${device.packageDir}/mod_voice.yaml`]);
  }
  if (has('idle')) pkgFiles.push(['mod_idle', `${device.packageDir}/mod_idle.yaml`]);
  if (has('settings')) pkgFiles.push(['mod_settings', `${device.packageDir}/mod_settings.yaml`]);

  if (state.packageSource === 'github') {
    const repo = (state.githubRepo || 'OWNER/Waveshare349').trim().replace(/^\/+|\/+$/g, '');
    const ref = (state.githubRef || 'main').trim();
    lines.push(`  remote_cfg:`);
    lines.push(`    url: https://github.com/${repo}`);
    lines.push(`    ref: ${ref}`);
    lines.push(`    refresh: 0s`);
    lines.push(`    files:`);
    for (const [, path] of pkgFiles) {
      lines.push(`      - ${path}`);
    }
  } else {
    for (const [key, path] of pkgFiles) {
      lines.push(`  ${key}: !include ${path}`);
    }
  }

  if (state.useApiSecret) {
    lines.push('');
    lines.push('api:');
    lines.push('  encryption:');
    lines.push('    key: !secret api_encryption_key');
  }

  lines.push('');
  return lines.join('\n');
}
