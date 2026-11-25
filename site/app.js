import {
  DEFAULT_GROWSPACES,
  FEATURE_MODULES,
  HA_COMPANION_GUIDES,
  PRESETS,
  TARGET_DEVICES,
} from './data.js';
import { generateEsphomeYaml } from './yaml-generator.js';

function detectGithubRepo() {
  const { hostname, pathname } = window.location;
  if (hostname.endsWith('.github.io')) {
    const owner = hostname.split('.')[0];
    const repo = pathname.split('/').filter(Boolean)[0] || 'Waveshare349';
    return `${owner}/${repo}`;
  }
  return 'OWNER/Waveshare349';
}

const state = {
  deviceId: 'waveshare-349',
  modules: [...PRESETS[0].modules],
  deviceName: 'waveshare-349-voice',
  friendlyName: 'Waveshare 3.49 Display',
  packageSource: 'github',
  githubRepo: detectGithubRepo(),
  githubRef: 'main',
  useImprov: false,
  useApiSecret: false,
  gsCount: 7,
  cycleInterval: 15,
  pauseTimeout: 25,
  idleTimeout: 300,
  growspaces: DEFAULT_GROWSPACES.map((g) => ({ ...g })),
  musicVariant: 'studio',
  musicPlayerEntity: 'sensor.music_assistant_bridge',
  claudeVariant: 'terminal',
  codexVariant: 'terminal',
  batteryEntity: 'sensor.m2101k7bny_battery_level',
  dirtyNames: false,
  dirtyGrowspaces: false,
};

const els = {
  deviceGrid: document.getElementById('device-grid'),
  presetBar: document.getElementById('preset-bar'),
  moduleGrid: document.getElementById('module-grid'),
  deviceName: document.getElementById('device-name'),
  friendlyName: document.getElementById('friendly-name'),
  packageSource: document.getElementById('package-source'),
  githubRepo: document.getElementById('github-repo'),
  githubRepoField: document.getElementById('github-repo-field'),
  useImprov: document.getElementById('use-improv'),
  useApiSecret: document.getElementById('use-api-secret'),
  moduleOptions: document.getElementById('module-options'),
  flashPanel: document.getElementById('flash-panel'),
  yamlOutput: document.getElementById('yaml-output'),
  copyYaml: document.getElementById('copy-yaml'),
  downloadYaml: document.getElementById('download-yaml'),
  haGuides: document.getElementById('ha-guides'),
  summaryList: document.getElementById('summary-list'),
  presetStatus: document.getElementById('preset-status'),
};

function currentDevice() {
  return TARGET_DEVICES.find((d) => d.id === state.deviceId) || TARGET_DEVICES[0];
}

function sameModuleSet(a, b) {
  if (a.length !== b.length) return false;
  const setA = new Set(a);
  return b.every((item) => setA.has(item));
}

function findMatchingPreset() {
  if (!state.useImprov || state.dirtyGrowspaces || Number(state.gsCount) !== 7) {
    return null;
  }
  const preset = PRESETS.find(
    (p) => p.deviceId === state.deviceId && sameModuleSet(p.modules, state.modules)
  );
  if (!preset) return null;

  const { defaults } = preset;
  if (defaults.musicVariant && state.musicVariant !== defaults.musicVariant) return null;
  if (defaults.claudeVariant && state.claudeVariant !== defaults.claudeVariant) return null;
  if (defaults.codexVariant && state.codexVariant !== defaults.codexVariant) return null;
  return preset;
}

function selectDevice(deviceId) {
  const dev = TARGET_DEVICES.find((d) => d.id === deviceId);
  if (!dev) return;
  state.deviceId = dev.id;
  state.modules = state.modules.filter((m) => dev.supportedModules.includes(m));
  if (state.modules.length === 0) {
    state.modules = ['grow'];
  }
  if (!state.dirtyNames) {
    state.deviceName = dev.defaultDeviceName;
    state.friendlyName = dev.defaultFriendlyName;
  }
  if (dev.id === 'prototype-480x320') {
    state.claudeVariant = 'terminal';
    state.musicPlayerEntity = 'sensor.music_assistant_bridge';
  } else {
    state.claudeVariant = 'terminal';
    state.musicPlayerEntity = 'sensor.music_assistant_bridge';
  }
  renderAll();
}

function toggleModule(modId) {
  const dev = currentDevice();
  if (!dev.supportedModules.includes(modId)) return;
  if (state.modules.includes(modId)) {
    if (state.modules.length === 1) return; // keep at least 1 module
    state.modules = state.modules.filter((m) => m !== modId);
  } else {
    state.modules = [...state.modules, modId];
  }
  renderAll();
}

function applyPreset(preset) {
  state.deviceId = preset.deviceId;
  state.modules = [...preset.modules];
  state.useImprov = true;
  state.gsCount = 7;
  state.dirtyGrowspaces = false;
  state.growspaces = DEFAULT_GROWSPACES.map((g) => ({ ...g }));
  if (preset.defaults.musicVariant) state.musicVariant = preset.defaults.musicVariant;
  if (preset.defaults.claudeVariant) state.claudeVariant = preset.defaults.claudeVariant;
  if (preset.defaults.codexVariant) state.codexVariant = preset.defaults.codexVariant;
  renderAll();
}

function renderDevices() {
  els.deviceGrid.innerHTML = '';
  for (const dev of TARGET_DEVICES) {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = `option-card${dev.id === state.deviceId ? ' selected' : ''}`;
    btn.setAttribute('role', 'radio');
    btn.setAttribute('aria-checked', String(dev.id === state.deviceId));
    btn.innerHTML = `
      <div class="option-top">
        <span class="option-title">${dev.name}</span>
        <span class="pill cyan">${dev.resolution}</span>
      </div>
      <div class="option-desc">${dev.subtitle} — ${dev.chip}</div>
    `;
    btn.addEventListener('click', () => selectDevice(dev.id));
    els.deviceGrid.appendChild(btn);
  }
}

function renderPresetsBar() {
  const devPresets = PRESETS.filter((p) => p.deviceId === state.deviceId);
  const activePreset = findMatchingPreset();
  els.presetBar.innerHTML = '<span class="muted" style="margin:0 4px 0 0;">Quick Presets:</span>';
  for (const preset of devPresets) {
    const chip = document.createElement('button');
    chip.type = 'button';
    chip.className = `preset-chip${activePreset?.id === preset.id ? ' active' : ''}`;
    chip.textContent = preset.name;
    chip.addEventListener('click', () => applyPreset(preset));
    els.presetBar.appendChild(chip);
  }
}

function renderModules() {
  const dev = currentDevice();
  els.moduleGrid.innerHTML = '';
  for (const mod of FEATURE_MODULES) {
    const supported = dev.supportedModules.includes(mod.id);
    const selected = supported && state.modules.includes(mod.id);
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = `option-card${selected ? ' selected' : ''}`;
    btn.disabled = !supported;
    btn.setAttribute('aria-pressed', String(selected));
    const reason = dev.unsupportedReasons[mod.id];
    btn.innerHTML = `
      <div class="option-top">
        <span class="option-title">${mod.name}</span>
        <span class="pill">${mod.kind}</span>
      </div>
      <div class="option-desc">${supported ? mod.description : reason}</div>
    `;
    btn.addEventListener('click', () => toggleModule(mod.id));
    els.moduleGrid.appendChild(btn);
  }
}

function renderModuleOptions() {
  const has = (id) => state.modules.includes(id);
  els.moduleOptions.innerHTML = '';

  if (has('grow')) {
    const panel = document.createElement('div');
    panel.className = 'sub-panel';
    panel.innerHTML = `
      <h4>Growspace Options</h4>
      <div class="form-grid">
        <label class="field">
          <span>Active Growspaces (1–7)</span>
          <input type="number" id="opt-gs-count" min="1" max="7" value="${state.gsCount}" />
        </label>
        <label class="field">
          <span>Auto-Cycle Interval (sec)</span>
          <input type="number" id="opt-cycle" min="5" max="120" value="${state.cycleInterval}" />
        </label>
        <label class="field">
          <span>Manual Pause Timeout (sec)</span>
          <input type="number" id="opt-pause" min="5" max="300" value="${state.pauseTimeout}" />
        </label>
      </div>
      <div class="growspaces-list" id="growspaces-list"></div>
    `;
    els.moduleOptions.appendChild(panel);

    const gsCountInput = panel.querySelector('#opt-gs-count');
    gsCountInput.addEventListener('input', (e) => {
      const val = Math.max(1, Math.min(7, Number(e.target.value) || 1));
      state.gsCount = val;
      renderGrowspaceInputs(panel.querySelector('#growspaces-list'));
      renderOutputAndSummary();
    });
    panel.querySelector('#opt-cycle').addEventListener('input', (e) => {
      state.cycleInterval = Math.max(5, Number(e.target.value) || 15);
      renderOutputAndSummary();
    });
    panel.querySelector('#opt-pause').addEventListener('input', (e) => {
      state.pauseTimeout = Math.max(5, Number(e.target.value) || 25);
      renderOutputAndSummary();
    });
    renderGrowspaceInputs(panel.querySelector('#growspaces-list'));
  }

  if (has('music')) {
    const panel = document.createElement('div');
    panel.className = 'sub-panel';
    panel.innerHTML = `
      <h4>Music Assistant Options</h4>
      <div class="form-grid">
        <label class="field">
          <span>Card Variant</span>
          <select id="opt-music-variant">
            <option value="studio" ${state.musicVariant === 'studio' ? 'selected' : ''}>Studio (EQ + Meter Console)</option>
            <option value="deck" ${state.musicVariant === 'deck' ? 'selected' : ''}>Deck (Vinyl Turntable)</option>
          </select>
        </label>
        <label class="field">
          <span>Player / Bridge Entity</span>
          <input type="text" id="opt-music-entity" value="${state.musicPlayerEntity}" />
        </label>
      </div>
    `;
    els.moduleOptions.appendChild(panel);
    panel.querySelector('#opt-music-variant').addEventListener('change', (e) => {
      state.musicVariant = e.target.value;
      renderOutputAndSummary();
    });
    panel.querySelector('#opt-music-entity').addEventListener('input', (e) => {
      state.musicPlayerEntity = e.target.value;
      state.dirtyGrowspaces = true;
      renderOutputAndSummary();
    });
  }

  if (has('claude') || has('codex')) {
    const aiVariants = [
      ['terminal', 'Terminal (CLI Status + Random Scenes)'],
      ['stage', 'Stage (Mascot-Forward + Random Scenes)'],
      ['fishing', 'Stage: Moonlit Pier (Fishing)'],
      ['mining', 'Stage: Crystal Cavern (Mining)'],
      ['pingpong', 'Stage: Arcade Rally (Ping-Pong)'],
      ['campfire', 'Stage: Starlit Campfire'],
      ['terminal_fishing', 'Terminal: Moonlit Pier (Fishing)'],
      ['terminal_mining', 'Terminal: Crystal Cavern (Mining)'],
      ['terminal_pingpong', 'Terminal: Arcade Rally (Ping-Pong)'],
      ['terminal_campfire', 'Terminal: Starlit Campfire'],
    ];
    const renderOpts = (cur) =>
      aiVariants
        .map(([val, label]) => `<option value="${val}" ${cur === val ? 'selected' : ''}>${label}</option>`)
        .join('');

    const panel = document.createElement('div');
    panel.className = 'sub-panel';
    panel.innerHTML = `
      <h4>AI Card Variants</h4>
      <div class="form-grid">
        ${
          has('claude')
            ? `<label class="field">
                <span>Claude Code Card</span>
                <select id="opt-claude-variant">${renderOpts(state.claudeVariant)}</select>
              </label>`
            : ''
        }
        ${
          has('codex')
            ? `<label class="field">
                <span>Codex Card</span>
                <select id="opt-codex-variant">${renderOpts(state.codexVariant)}</select>
              </label>`
            : ''
        }
      </div>
    `;
    els.moduleOptions.appendChild(panel);
    const claudeSel = panel.querySelector('#opt-claude-variant');
    if (claudeSel) {
      claudeSel.addEventListener('change', (e) => {
        state.claudeVariant = e.target.value;
        renderOutputAndSummary();
      });
    }
    const codexSel = panel.querySelector('#opt-codex-variant');
    if (codexSel) {
      codexSel.addEventListener('change', (e) => {
        state.codexVariant = e.target.value;
        renderOutputAndSummary();
      });
    }
  }

  if (has('idle') || state.deviceId === 'prototype-480x320') {
    const panel = document.createElement('div');
    panel.className = 'sub-panel';
    panel.innerHTML = `
      <h4>Idle Lock &amp; Hardware Options</h4>
      <div class="form-grid">
        ${
          has('idle')
            ? `<label class="field">
                <span>Idle Lock Timeout (sec)</span>
                <input type="number" id="opt-idle-timeout" min="30" max="3600" value="${state.idleTimeout}" />
              </label>`
            : ''
        }
        ${
          state.deviceId === 'prototype-480x320'
            ? `<label class="field">
                <span>Battery Sensor Entity (HA)</span>
                <input type="text" id="opt-battery-entity" value="${state.batteryEntity}" />
              </label>`
            : ''
        }
      </div>
    `;
    els.moduleOptions.appendChild(panel);
    const idleInput = panel.querySelector('#opt-idle-timeout');
    if (idleInput) {
      idleInput.addEventListener('input', (e) => {
        state.idleTimeout = Math.max(30, Number(e.target.value) || 300);
        renderOutputAndSummary();
      });
    }
    const batInput = panel.querySelector('#opt-battery-entity');
    if (batInput) {
      batInput.addEventListener('input', (e) => {
        state.batteryEntity = e.target.value;
        state.dirtyGrowspaces = true;
        renderOutputAndSummary();
      });
    }
  }
}

function renderGrowspaceInputs(container) {
  container.innerHTML = '';
  for (let i = 0; i < state.gsCount; i++) {
    const label = document.createElement('label');
    label.className = 'field';
    label.innerHTML = `
      <span>Space ${i} Name</span>
      <input type="text" value="${state.growspaces[i].name}" />
    `;
    label.querySelector('input').addEventListener('input', (e) => {
      state.growspaces[i].name = e.target.value;
      state.growspaces[i].slug = e.target.value
        .toLowerCase()
        .trim()
        .replace(/[^a-z0-9]+/g, '_')
        .replace(/^_+|_+$/g, '');
      state.dirtyGrowspaces = true;
      renderOutputAndSummary();
    });
    container.appendChild(label);
  }
}

function renderFlashPanel(preset) {
  if (preset) {
    const manifestPath = `firmware/${preset.id}.manifest.json`;
    els.flashPanel.innerHTML = `
      <div class="flash-ready">
        <div>
          <span class="flash-badge ok">Pre-built Preset Matched: ${preset.name}</span>
          <p class="muted" style="margin:0;">
            Connect your ESP32-S3 via USB in Chrome or Edge to flash directly, then configure Wi-Fi via Improv Serial.
          </p>
        </div>
        <esp-web-install-button manifest="${manifestPath}">
          <button slot="activate" type="button" class="btn btn-primary">Install via USB (Web Serial)</button>
        </esp-web-install-button>
      </div>
    `;
  } else {
    els.flashPanel.innerHTML = `
      <span class="flash-badge warn">Custom Build Configuration</span>
      <p class="muted" style="margin:0;">
        Your selection uses custom modules, variants, or entity names that don't match a pre-built CI preset.
        Download the generated <code>.yaml</code> below and flash it using <strong>Home Assistant ESPHome Builder</strong> or
        <code>esphome run ${state.deviceName}.yaml</code>.
      </p>
    `;
  }
}

function renderHaGuides() {
  els.haGuides.innerHTML = '';
  for (const modId of state.modules) {
    const guide = HA_COMPANION_GUIDES[modId];
    if (!guide) continue;
    const card = document.createElement('div');
    card.className = 'ha-card';
    card.innerHTML = `
      <div class="ha-card-head">
        <strong>${guide.title}</strong>
        <code>${guide.file}</code>
      </div>
      <p class="muted" style="margin:0;">${guide.summary}</p>
      <ul>${guide.steps.map((s) => `<li>${s}</li>`).join('')}</ul>
    `;
    els.haGuides.appendChild(card);
  }
}

function renderSummary(preset) {
  const dev = currentDevice();
  const displayViews = state.modules.filter((m) => m !== 'voice').length;
  els.summaryList.innerHTML = `
    <dt>Target Device</dt><dd>${dev.name}</dd>
    <dt>Resolution</dt><dd>${dev.resolution}</dd>
    <dt>Feature Modules</dt><dd>${state.modules.length} selected</dd>
    <dt>Nav Rail</dt><dd>${displayViews <= 1 ? 'Hidden (single view)' : `${displayViews} views`}</dd>
    <dt>Wi-Fi Mode</dt><dd>${state.useImprov ? 'Improv + Captive Portal' : '!secret wifi_*'}</dd>
  `;
  els.presetStatus.innerHTML = preset
    ? `Web Serial flash ready: <strong>${preset.name}</strong>`
    : 'Custom YAML mode — compile with ESPHome Builder or CLI.';
}

function renderOutputAndSummary() {
  const preset = findMatchingPreset();
  renderFlashPanel(preset);
  renderSummary(preset);
  els.yamlOutput.textContent = generateEsphomeYaml(state);
}

function syncStaticInputs() {
  els.deviceName.value = state.deviceName;
  els.friendlyName.value = state.friendlyName;
  els.packageSource.value = state.packageSource;
  els.githubRepo.value = state.githubRepo;
  els.githubRepoField.style.display = state.packageSource === 'github' ? 'flex' : 'none';
  els.useImprov.checked = state.useImprov;
  els.useApiSecret.checked = state.useApiSecret;
}

function renderAll() {
  syncStaticInputs();
  renderDevices();
  renderPresetsBar();
  renderModules();
  renderModuleOptions();
  renderHaGuides();
  renderOutputAndSummary();
}

els.deviceName.addEventListener('input', (e) => {
  state.deviceName = e.target.value;
  state.dirtyNames = true;
  renderOutputAndSummary();
});

els.friendlyName.addEventListener('input', (e) => {
  state.friendlyName = e.target.value;
  state.dirtyNames = true;
  renderOutputAndSummary();
});

els.packageSource.addEventListener('change', (e) => {
  state.packageSource = e.target.value;
  els.githubRepoField.style.display = state.packageSource === 'github' ? 'flex' : 'none';
  renderOutputAndSummary();
});

els.githubRepo.addEventListener('input', (e) => {
  state.githubRepo = e.target.value;
  renderOutputAndSummary();
});

els.useImprov.addEventListener('change', (e) => {
  state.useImprov = e.target.checked;
  renderOutputAndSummary();
});

els.useApiSecret.addEventListener('change', (e) => {
  state.useApiSecret = e.target.checked;
  renderOutputAndSummary();
});

els.copyYaml.addEventListener('click', async () => {
  await navigator.clipboard.writeText(els.yamlOutput.textContent);
  els.copyYaml.textContent = 'Copied!';
  setTimeout(() => {
    els.copyYaml.textContent = 'Copy YAML';
  }, 1600);
});

els.downloadYaml.addEventListener('click', () => {
  const blob = new Blob([els.yamlOutput.textContent], { type: 'text/yaml' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `${state.deviceName || 'device'}.yaml`;
  a.click();
  URL.revokeObjectURL(url);
});

renderAll();
