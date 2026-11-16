#pragma once
// Synthesized 16 kHz 16-bit mono PCM chimes for Claude Code and Codex
// Attention States (Permission Request & Needs Input), plus transition
// tracking that suppresses boot/reconnect replays and duplicate updates.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ai_sounds {

constexpr uint32_t SAMPLE_RATE = 16000;
constexpr float TWO_PI = 6.28318530717958647692f;
constexpr float MAX_PEAK = 18000.0f;  // Leaves headroom on ES8311 16-bit DAC

enum ChimeId : uint8_t {
  NONE = 0,
  CLAUDE_PERMISSION,
  CLAUDE_INPUT,
  CODEX_PERMISSION,
  CODEX_INPUT,
};

struct Note {
  float freq_hz;
  uint16_t start_ms;
  uint16_t dur_ms;
  float harmonic_mix;  // 2nd/3rd harmonic blend for timbre
  uint8_t harmonic_mul;
};

// Synthesize a sequence of overlapping notes into 16-bit signed mono PCM.
inline std::vector<int16_t> synthesize(const Note *notes, size_t count, float volume_pct) {
  float vol = std::max(0.0f, std::min(100.0f, volume_pct)) / 100.0f;
  if (vol <= 0.0f || count == 0) return {};

  uint32_t total_ms = 0;
  for (size_t i = 0; i < count; ++i) {
    uint32_t end_ms = static_cast<uint32_t>(notes[i].start_ms) + notes[i].dur_ms;
    if (end_ms > total_ms) total_ms = end_ms;
  }

  const size_t total_samples = (total_ms * SAMPLE_RATE) / 1000;
  std::vector<float> mix(total_samples, 0.0f);

  for (size_t i = 0; i < count; ++i) {
    const Note &n = notes[i];
    const size_t start_idx = (static_cast<size_t>(n.start_ms) * SAMPLE_RATE) / 1000;
    const size_t len = (static_cast<size_t>(n.dur_ms) * SAMPLE_RATE) / 1000;
    const size_t attack = std::max<size_t>(1, (8 * SAMPLE_RATE) / 1000);  // 8 ms soft attack

    for (size_t s = 0; s < len && (start_idx + s) < total_samples; ++s) {
      float t = static_cast<float>(s) / static_cast<float>(SAMPLE_RATE);
      float env = (s < attack)
          ? 0.5f * (1.0f - std::cos(3.14159265f * static_cast<float>(s) / static_cast<float>(attack)))
          : std::exp(-5.0f * static_cast<float>(s - attack) / static_cast<float>(len));

      float wave = std::sin(TWO_PI * n.freq_hz * t);
      if (n.harmonic_mix > 0.0f) {
        wave += n.harmonic_mix * std::sin(TWO_PI * (n.freq_hz * n.harmonic_mul) * t);
        wave /= (1.0f + n.harmonic_mix);
      }
      mix[start_idx + s] += wave * env;
    }
  }

  std::vector<int16_t> pcm(total_samples);
  const float scale = MAX_PEAK * vol;
  for (size_t i = 0; i < total_samples; ++i) {
    float clamped = std::max(-1.0f, std::min(1.0f, mix[i]));
    pcm[i] = static_cast<int16_t>(clamped * scale);
  }
  return pcm;
}

inline std::vector<int16_t> render_chime(ChimeId id, float volume_pct) {
  // Claude: warm marimba-like 2-note motifs (2nd harmonic).
  static constexpr Note kClaudePerm[] = {
      {587.33f, 0, 140, 0.25f, 2},    // D5
      {880.00f, 95, 240, 0.25f, 2},   // A5
  };
  static constexpr Note kClaudeInput[] = {
      {659.25f, 0, 120, 0.22f, 2},    // E5
      {880.00f, 90, 210, 0.22f, 2},   // A5
  };
  // Codex: crisp tech arpeggio/ping motifs (3rd harmonic).
  static constexpr Note kCodexPerm[] = {
      {523.25f, 0, 90, 0.16f, 3},     // C5
      {659.25f, 70, 90, 0.16f, 3},    // E5
      {783.99f, 140, 210, 0.16f, 3},  // G5
  };
  static constexpr Note kCodexInput[] = {
      {783.99f, 0, 100, 0.15f, 3},    // G5
      {1046.50f, 80, 190, 0.15f, 3},  // C6
  };

  switch (id) {
    case CLAUDE_PERMISSION:
      return synthesize(kClaudePerm, sizeof(kClaudePerm) / sizeof(kClaudePerm[0]), volume_pct);
    case CLAUDE_INPUT:
      return synthesize(kClaudeInput, sizeof(kClaudeInput) / sizeof(kClaudeInput[0]), volume_pct);
    case CODEX_PERMISSION:
      return synthesize(kCodexPerm, sizeof(kCodexPerm) / sizeof(kCodexPerm[0]), volume_pct);
    case CODEX_INPUT:
      return synthesize(kCodexInput, sizeof(kCodexInput) / sizeof(kCodexInput[0]), volume_pct);
    default:
      return {};
  }
}

struct AgentTracker {
  bool primed = false;
  uint8_t last_mode = 0;

  template <typename ModeT, typename ParseFn>
  ChimeId on_status(const std::string &raw, ParseFn parse_fn, bool is_codex) {
    if (raw.empty() || raw == "unknown" || raw == "unavailable") {
      return NONE;
    }
    ModeT m = parse_fn(raw);
    uint8_t code = static_cast<uint8_t>(m);
    if (!primed) {
      primed = true;
      last_mode = code;
      return NONE;
    }
    if (code == last_mode) {
      return NONE;
    }
    last_mode = code;
    if (m == ModeT::PENDING) return is_codex ? CODEX_PERMISSION : CLAUDE_PERMISSION;
    if (m == ModeT::NEEDS_INPUT) return is_codex ? CODEX_INPUT : CLAUDE_INPUT;
    return NONE;
  }
};

inline AgentTracker &claude_tracker() {
  static AgentTracker t;
  return t;
}

inline AgentTracker &codex_tracker() {
  static AgentTracker t;
  return t;
}

template <typename SpeakerT>
inline bool maybe_play(SpeakerT *spk, ChimeId id, bool enabled, float volume_pct, bool voice_active) {
  if (id == NONE || !enabled || voice_active || spk == nullptr) return false;
  if (spk->is_running()) return false;
  static std::vector<int16_t> pcm;
  pcm = render_chime(id, volume_pct);
  if (pcm.empty()) return false;
  spk->play(reinterpret_cast<const uint8_t *>(pcm.data()), pcm.size() * sizeof(int16_t));
  return true;
}

}  // namespace ai_sounds
