#pragma once
// Animated pixel-art Music Assistant sidebar icon, drawn as an LVGL pixel
// 4-band spectrum meter with falling peak-hold caps on the 14 x 11 unit grid
// (u = 2 px -> 28 x 22 px), matching Clawd and Byte.
//
//   ..CC..CC..CC..CC..   C peak-hold cap (2u x 1u)
//   ..BB..BB..BB..BB..   B spectrum bar (2u wide, 2..8u tall)
//   ..BB..BB..BB..BB..
//   .SSSSSSSSSSSSSSSS.   S floor shadow
//
#include <lvgl.h>
#include <cmath>
#include <cstdint>

namespace music_icon {

// ── Shared runtime state ───────────────────────────────────────────────────
inline bool g_playing = false;
inline bool g_muted = false;
inline bool g_active = false;
inline uint32_t g_frame = 0;  // 10 Hz ticks since boot

// Studio-inspired 4-bar gradient when playing
constexpr uint32_t BAR_COLORS[4] = { 0x58A6FF, 0x79C0FF, 0xA371F7, 0xF778BA };
constexpr uint32_t CAP_PLAY = 0xFFFFFF;
constexpr uint32_t BAR_IDLE_ACTIVE = 0xC9D1D9;
constexpr uint32_t BAR_IDLE_INACTIVE = 0x8B949E;
constexpr uint32_t BAR_MUTED = 0xF85149;
constexpr uint32_t SHADOW = 0x000000;

struct Equalizer {
  lv_obj_t *root = nullptr;
  lv_obj_t *shadow = nullptr;
  lv_obj_t *bar[4] = {nullptr, nullptr, nullptr, nullptr};
  lv_obj_t *cap[4] = {nullptr, nullptr, nullptr, nullptr};
  int cur_h[4] = {6, 12, 8, 10};
  int peak_h[4] = {6, 12, 8, 10};
  int peak_hold[4] = {0, 0, 0, 0};
  int u = 2;
};

inline lv_obj_t *block(lv_obj_t *parent, uint32_t color, lv_opa_t opa = LV_OPA_COVER) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_remove_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  return o;
}

inline void build(Equalizer &eq, lv_obj_t *root, int u = 2) {
  eq.root = root;
  eq.u = u;
  lv_obj_set_size(root, 14 * u, 11 * u);
  lv_obj_add_flag(root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_remove_flag(root, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

  eq.shadow = block(root, SHADOW, LV_OPA_40);
  lv_obj_set_style_radius(eq.shadow, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_pos(eq.shadow, u, 10 * u);
  lv_obj_set_size(eq.shadow, 12 * u, u);

  for (int i = 0; i < 4; i++) {
    eq.bar[i] = block(root, BAR_IDLE_INACTIVE);
    eq.cap[i] = block(root, CAP_PLAY);
  }
}

inline void apply(Equalizer &eq) {
  const int u = eq.u;
  const int floor_y = 10 * u;  // 20 px
  const int max_h = 8 * u;     // 16 px
  const int min_h = 2 * u;     // 4 px
  const uint32_t f = g_frame;

  // Resting chord silhouette when paused/idle
  static const int REST_H[4] = {6, 12, 8, 10};

  for (int i = 0; i < 4; i++) {
    int target_h;
    uint32_t bar_col;
    uint32_t cap_col;

    if (g_muted) {
      target_h = min_h;
      bar_col = BAR_MUTED;
      cap_col = ((f % 12) < 6) ? BAR_MUTED : BAR_IDLE_INACTIVE;
    } else if (g_playing) {
      float wave = 0.5f + 0.5f * sinf(f * 0.48f + i * 1.15f) * cosf(f * 0.23f - i * 0.45f);
      target_h = min_h + (int) (wave * (max_h - min_h));
      // Quantize to u/2 (1px) or u (2px) for crisp pixel-art steps
      target_h = ((target_h + 1) / u) * u;
      if (target_h < min_h) target_h = min_h;
      if (target_h > max_h) target_h = max_h;
      bar_col = BAR_COLORS[i];
      cap_col = CAP_PLAY;
    } else {
      // Gentle idle breath on the resting chord
      int breath = ((f / 16 + i) % 2 == 0) ? 0 : 1;
      target_h = REST_H[i] + (i == 1 || i == 2 ? breath : 0);
      bar_col = g_active ? BAR_IDLE_ACTIVE : BAR_IDLE_INACTIVE;
      cap_col = g_active ? 0x58A6FF : BAR_IDLE_INACTIVE;
    }

    // Smoothly settle down when pausing, snap up when playing
    if (g_playing && !g_muted) {
      eq.cur_h[i] = target_h;
    } else {
      if (eq.cur_h[i] > target_h) eq.cur_h[i] -= (eq.cur_h[i] - target_h > 2) ? 2 : 1;
      else if (eq.cur_h[i] < target_h) eq.cur_h[i] += 1;
    }

    // Peak-hold cap physics
    if (eq.cur_h[i] >= eq.peak_h[i]) {
      eq.peak_h[i] = eq.cur_h[i];
      eq.peak_hold[i] = 3;
    } else if (eq.peak_hold[i] > 0) {
      eq.peak_hold[i]--;
    } else if (eq.peak_h[i] > eq.cur_h[i]) {
      eq.peak_h[i]--;
    }

    const int x = (1 + i * 3) * u + u / 2;  // 3, 9, 15, 21 px -> centered in 28 px
    const int h = eq.cur_h[i];
    lv_obj_set_pos(eq.bar[i], x, floor_y - h);
    lv_obj_set_size(eq.bar[i], 2 * u, h);
    lv_obj_set_style_bg_color(eq.bar[i], lv_color_hex(bar_col), 0);

    const int cap_y = floor_y - eq.peak_h[i] - u - 1;
    lv_obj_set_pos(eq.cap[i], x, cap_y < 0 ? 0 : cap_y);
    lv_obj_set_size(eq.cap[i], 2 * u, u);
    lv_obj_set_style_bg_color(eq.cap[i], lv_color_hex(cap_col), 0);
  }
}

// Update the sidebar nav Music icon at 10 Hz.
inline void tick_nav(lv_obj_t *box) {
  static Equalizer eq;
  if (eq.root == nullptr) build(eq, box, 2);
  g_frame++;
  apply(eq);
}

}  // namespace music_icon
