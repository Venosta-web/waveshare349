#pragma once
// Animated pixel-art Growspace sidebar icon, drawn as an LVGL pixel sprite
// matching the 14 x 11 unit grid (u = 2 px -> 28 x 22 px) of Clawd and Byte.
//
//   ......TT......   T top bud / shoot (perks up, sways in breeze, pulses on alert)
//   ..LL..TT..RR..   L left leaf, R right leaf
//   .LLLL.SS.RRRR.   S stem
//   ..LL..SS..RR..   M soil / mulch rim
//   ......SS......   P terracotta pot
//   ...MMMMMMMM...   * rising humidifier mist pixel (when humidifier is on)
//   ..PPPPPPPPPP..
//   ...PPPPPPPP...
//   ...PPPPPPPP...
//   ..SSSSSSSSSS..   floor shadow
//
#include <lvgl.h>
#include <cstdint>

namespace grow_icon {

enum State : uint8_t { OPTIMAL, WARN, DANGER };

// ── Shared runtime state ───────────────────────────────────────────────────
inline State g_state = OPTIMAL;
inline bool g_is_day = true;
inline bool g_humid_on = false;
inline bool g_fan_on = false;
inline bool g_active = true;
inline uint32_t g_frame = 0;  // 10 Hz ticks since boot

// Palette
constexpr uint32_t LEAF_DAY = 0x3FB950;
constexpr uint32_t LEAF_NIGHT = 0x238636;
constexpr uint32_t BUD_DAY = 0x7EE787;
constexpr uint32_t BUD_NIGHT = 0x3FB950;
constexpr uint32_t STEM_DAY = 0x2EA043;
constexpr uint32_t STEM_NIGHT = 0x196C2E;
constexpr uint32_t SOIL = 0x6E4E37;
constexpr uint32_t POT_RIM = 0xD97757;
constexpr uint32_t POT_BODY = 0xB85D43;
constexpr uint32_t MIST = 0x64B5F6;
constexpr uint32_t WARN_COL = 0xFFC107;
constexpr uint32_t DANGER_COL = 0xFF5252;
constexpr uint32_t SHADOW = 0x000000;

struct Sprite {
  lv_obj_t *root = nullptr;
  lv_obj_t *shadow = nullptr;
  lv_obj_t *pot_body = nullptr;
  lv_obj_t *pot_rim = nullptr;
  lv_obj_t *soil = nullptr;
  lv_obj_t *stem = nullptr;
  lv_obj_t *leaf_l = nullptr;
  lv_obj_t *leaf_l_tip = nullptr;
  lv_obj_t *leaf_r = nullptr;
  lv_obj_t *leaf_r_tip = nullptr;
  lv_obj_t *bud = nullptr;
  lv_obj_t *bud_hi = nullptr;
  lv_obj_t *mist = nullptr;
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

inline void build(Sprite &s, lv_obj_t *root, int u = 2) {
  s.root = root;
  s.u = u;
  lv_obj_set_size(root, 14 * u, 11 * u);
  lv_obj_add_flag(root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_remove_flag(root, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

  s.shadow = block(root, SHADOW, LV_OPA_40);
  lv_obj_set_style_radius(s.shadow, LV_RADIUS_CIRCLE, 0);

  s.pot_body = block(root, POT_BODY);
  s.pot_rim = block(root, POT_RIM);
  s.soil = block(root, SOIL);
  s.stem = block(root, STEM_DAY);

  s.leaf_l = block(root, LEAF_DAY);
  s.leaf_l_tip = block(root, LEAF_DAY);
  s.leaf_r = block(root, LEAF_DAY);
  s.leaf_r_tip = block(root, LEAF_DAY);

  s.bud = block(root, LEAF_DAY);
  s.bud_hi = block(root, BUD_DAY);
  s.mist = block(root, MIST, LV_OPA_80);
  lv_obj_add_flag(s.mist, LV_OBJ_FLAG_HIDDEN);
}

inline void apply(Sprite &s) {
  const int u = s.u;
  const uint32_t f = g_frame;

  // Breathing / breeze cadence: livelier when circulation/exhaust fans run.
  const int breath_period = g_fan_on ? 16 : 32;
  const int phase = (int) (f % breath_period);
  const int half = breath_period / 2;
  const int sway_l = (phase < half / 2) ? -1 : 0;
  const int sway_r = (phase >= half && phase < half + half / 2) ? -1 : 0;
  const int bud_dy = (phase < half) ? 0 : 1;
  const int bud_dx = g_fan_on ? ((phase == 2 || phase == 3) ? 1 : (phase == 10 || phase == 11) ? -1 : 0) : 0;

  // Nighttime rest: leaves relax 1px lower.
  const int night_dy = g_is_day ? 0 : 1;

  // Danger shiver every 1.2s (12 frames).
  int shiver_dx = 0;
  if (g_state == DANGER) {
    static const int SHIVER[] = {1, -1, 1, 0};
    uint32_t df = f % 12;
    if (df < 4) shiver_dx = SHIVER[df];
  }

  // Palette based on day/night & alert state.
  uint32_t leaf_c = g_is_day ? LEAF_DAY : LEAF_NIGHT;
  uint32_t bud_c = g_is_day ? BUD_DAY : BUD_NIGHT;
  uint32_t stem_c = g_is_day ? STEM_DAY : STEM_NIGHT;
  uint32_t rim_c = POT_RIM;

  if (g_state == WARN && (f % 10) < 4) {
    bud_c = WARN_COL;
  } else if (g_state == DANGER && (f % 8) < 4) {
    bud_c = DANGER_COL;
    rim_c = DANGER_COL;
  }

  // Floor shadow & pot (anchored at bottom of 28 x 22 box).
  lv_obj_set_pos(s.shadow, 3 * u + u / 2, 10 * u);
  lv_obj_set_size(s.shadow, 7 * u, u);

  lv_obj_set_pos(s.pot_body, 4 * u + shiver_dx, 8 * u);
  lv_obj_set_size(s.pot_body, 6 * u, 2 * u);

  lv_obj_set_pos(s.pot_rim, 3 * u + shiver_dx, 7 * u);
  lv_obj_set_size(s.pot_rim, 8 * u, u);
  lv_obj_set_style_bg_color(s.pot_rim, lv_color_hex(rim_c), 0);

  lv_obj_set_pos(s.soil, 4 * u + shiver_dx, 6 * u + u / 2);
  lv_obj_set_size(s.soil, 6 * u, u / 2);

  // Stem (1u wide, centered at x = 6.5u -> 13px).
  const int stem_top = 3 * u + bud_dy;
  lv_obj_set_pos(s.stem, 6 * u + u / 2 + shiver_dx, stem_top);
  lv_obj_set_size(s.stem, u, 6 * u + u / 2 - stem_top);
  lv_obj_set_style_bg_color(s.stem, lv_color_hex(stem_c), 0);

  // Left leaf (tapered 2-part pixel cluster).
  const int ly = 3 * u + sway_l + night_dy;
  lv_obj_set_pos(s.leaf_l, 3 * u + shiver_dx, ly);
  lv_obj_set_size(s.leaf_l, 3 * u, 2 * u);
  lv_obj_set_style_bg_color(s.leaf_l, lv_color_hex(leaf_c), 0);

  lv_obj_set_pos(s.leaf_l_tip, 2 * u + shiver_dx, ly + u / 2);
  lv_obj_set_size(s.leaf_l_tip, u, u);
  lv_obj_set_style_bg_color(s.leaf_l_tip, lv_color_hex(leaf_c), 0);

  // Right leaf (mirrored).
  const int ry = 3 * u + sway_r + night_dy;
  lv_obj_set_pos(s.leaf_r, 8 * u + shiver_dx, ry);
  lv_obj_set_size(s.leaf_r, 3 * u, 2 * u);
  lv_obj_set_style_bg_color(s.leaf_r, lv_color_hex(leaf_c), 0);

  lv_obj_set_pos(s.leaf_r_tip, 11 * u + shiver_dx, ry + u / 2);
  lv_obj_set_size(s.leaf_r_tip, u, u);
  lv_obj_set_style_bg_color(s.leaf_r_tip, lv_color_hex(leaf_c), 0);

  // Top bud & highlight.
  const int bx = 6 * u + bud_dx + shiver_dx;
  const int by = u + bud_dy;
  lv_obj_set_pos(s.bud, bx, by);
  lv_obj_set_size(s.bud, 2 * u, 2 * u);
  lv_obj_set_style_bg_color(s.bud, lv_color_hex(leaf_c), 0);

  lv_obj_set_pos(s.bud_hi, bx + u / 2, by);
  lv_obj_set_size(s.bud_hi, u, u);
  lv_obj_set_style_bg_color(s.bud_hi, lv_color_hex(bud_c), 0);

  // Rising mist droplet when humidifier is active.
  if (g_humid_on) {
    lv_obj_clear_flag(s.mist, LV_OBJ_FLAG_HIDDEN);
    const int m_step = (int) (f % 12);
    const int mx = (m_step < 6) ? (4 * u) : (9 * u);
    const int my = 2 * u - (m_step % 6);
    lv_obj_set_pos(s.mist, mx, my);
    lv_obj_set_size(s.mist, u, u);
    lv_obj_set_style_bg_opa(s.mist, (m_step % 6) < 4 ? LV_OPA_80 : LV_OPA_30, 0);
  } else {
    lv_obj_add_flag(s.mist, LV_OBJ_FLAG_HIDDEN);
  }
}

// Update the sidebar nav Grow icon so it always mirrors live growspace state.
inline void tick_nav(lv_obj_t *box) {
  static Sprite sp;
  if (sp.root == nullptr) build(sp, box, 2);
  g_frame++;
  apply(sp);
}

}  // namespace grow_icon
