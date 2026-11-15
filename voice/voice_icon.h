#pragma once
// Custom LVGL sidebar icon for AI Voice Activation (5-bar acoustic waveform +
// 4-point AI neural spark + muted slash). Driven at 10 Hz by mod_voice.yaml so
// the sidebar tile always communicates live Voice Satellite state at a glance.
#include <lvgl.h>
#include <cstdint>

namespace voice_icon {

enum Mode : uint8_t { IDLE, LISTENING, THINKING, SPEAKING, MUTED, ERROR };

inline Mode g_mode = IDLE;
inline uint32_t g_frame = 0;

struct Icon {
  lv_obj_t *root = nullptr;
  lv_obj_t *bars[5] = {};
  lv_obj_t *spark_h = nullptr;
  lv_obj_t *spark_v = nullptr;
  lv_obj_t *spark_c = nullptr;
  lv_obj_t *slash[5] = {};
};

inline lv_obj_t *pill(lv_obj_t *parent, uint32_t color, lv_opa_t opa = LV_OPA_COVER) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_remove_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  return o;
}

inline void set_mode(Mode m) {
  g_mode = m;
}

inline void build(Icon &ic, lv_obj_t *root) {
  ic.root = root;
  lv_obj_set_size(root, 42, 28);
  lv_obj_remove_flag(root, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

  for (int i = 0; i < 5; i++) {
    ic.bars[i] = pill(root, 0x58A6FF);
  }

  // 4-point AI neural spark at top-right shoulder (cx=35, cy=6)
  ic.spark_h = pill(root, 0x79C0FF);
  ic.spark_v = pill(root, 0x79C0FF);
  ic.spark_c = pill(root, 0xE6EDF3);

  // Stepped diagonal mute slash across the waveform (hidden unless MUTED)
  for (int i = 0; i < 5; i++) {
    ic.slash[i] = pill(root, 0xF85149);
    lv_obj_set_size(ic.slash[i], 5, 2);
    lv_obj_set_pos(ic.slash[i], 6 + i * 5, 6 + i * 3);
    lv_obj_add_flag(ic.slash[i], LV_OBJ_FLAG_HIDDEN);
  }
}

inline void apply_btn_style(lv_obj_t *btn, uint32_t bg, uint32_t border) {
  if (!btn) return;
  lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(border), 0);
}

inline void set_bar(lv_obj_t *bar, int idx, int h, uint32_t color, lv_opa_t opa = LV_OPA_COVER) {
  const int x = 4 + idx * 6;  // 5 bars: x = 4, 10, 16, 22, 28 (3px wide, 3px gap)
  const int y_mid = 15;
  const int y = y_mid - h / 2;
  lv_obj_set_pos(bar, x, y);
  lv_obj_set_size(bar, 3, h);
  lv_obj_set_style_bg_color(bar, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(bar, opa, 0);
}

inline void set_spark(Icon &ic, int arm_len, uint32_t arm_col, uint32_t core_col, lv_opa_t opa) {
  const int cx = 35;
  const int cy = 6;
  lv_obj_set_pos(ic.spark_h, cx - arm_len / 2, cy);
  lv_obj_set_size(ic.spark_h, arm_len, 1);
  lv_obj_set_style_bg_color(ic.spark_h, lv_color_hex(arm_col), 0);
  lv_obj_set_style_bg_opa(ic.spark_h, opa, 0);

  lv_obj_set_pos(ic.spark_v, cx, cy - arm_len / 2);
  lv_obj_set_size(ic.spark_v, 1, arm_len);
  lv_obj_set_style_bg_color(ic.spark_v, lv_color_hex(arm_col), 0);
  lv_obj_set_style_bg_opa(ic.spark_v, opa, 0);

  lv_obj_set_pos(ic.spark_c, cx - 1, cy - 1);
  lv_obj_set_size(ic.spark_c, 3, 3);
  lv_obj_set_style_bg_color(ic.spark_c, lv_color_hex(core_col), 0);
  lv_obj_set_style_bg_opa(ic.spark_c, opa, 0);
}

inline void tick_nav(lv_obj_t *box, lv_obj_t *btn) {
  static Icon ic;
  if (ic.root == nullptr) build(ic, box);

  const uint32_t f = g_frame++;
  const bool muted = (g_mode == MUTED);

  for (int i = 0; i < 5; i++) {
    if (muted) lv_obj_clear_flag(ic.slash[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(ic.slash[i], LV_OBJ_FLAG_HIDDEN);
  }

  switch (g_mode) {
    case IDLE: {
      // Poised wake-word-armed silhouette with a slow, calm 0.4 Hz breath
      static const int BREATH[] = {0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
      const int b = BREATH[f % 20];
      const int h[5] = {6, 12 + b, 18 + b, 12 + b, 6};
      const uint32_t c[5] = {0x2B5B84, 0x388BFD, 0x58A6FF, 0x388BFD, 0x2B5B84};
      for (int i = 0; i < 5; i++) set_bar(ic.bars[i], i, h[i], c[i]);
      const bool twinkle = (f % 40) < 4;
      set_spark(ic, twinkle ? 7 : 5, 0x79C0FF, twinkle ? 0xFFFFFF : 0xA5D6FF, LV_OPA_COVER);
      apply_btn_style(btn, 0x101926, 0x26466D);
      break;
    }

    case LISTENING: {
      // Energetic 10 Hz voice-reactive bars in electric blue
      static const int SEQ[8][5] = {
        { 8, 16, 22, 14,  8},
        {12, 20, 16, 18, 10},
        { 6, 14, 24, 20, 12},
        {10, 22, 18, 12,  8},
        {14, 16, 20, 22, 10},
        { 8, 12, 24, 16, 14},
        {10, 18, 14, 20,  8},
        { 6, 20, 22, 14, 10},
      };
      const int *h = SEQ[f % 8];
      const uint32_t c[5] = {0x388BFD, 0x58A6FF, 0x79C0FF, 0x58A6FF, 0x388BFD};
      for (int i = 0; i < 5; i++) set_bar(ic.bars[i], i, h[i], c[i]);
      set_spark(ic, (f % 4) < 2 ? 7 : 5, 0xA5D6FF, 0xFFFFFF, LV_OPA_COVER);
      apply_btn_style(btn, 0x0F2744, 0x58A6FF);
      break;
    }

    case THINKING: {
      // Smooth left-to-right scanning wave in amber gold
      const int active = (f / 2) % 5;
      for (int i = 0; i < 5; i++) {
        const int dist = i > active ? i - active : active - i;
        const int h = dist == 0 ? 20 : (dist == 1 ? 13 : 6);
        const uint32_t col = dist == 0 ? 0xFFD166 : (dist == 1 ? 0xFFB020 : 0x7A5610);
        set_bar(ic.bars[i], i, h, col);
      }
      set_spark(ic, (f % 4) < 2 ? 7 : 5, 0xFFD166, 0xFFF3BF, LV_OPA_COVER);
      apply_btn_style(btn, 0x261C08, 0xFFB020);
      break;
    }

    case SPEAKING: {
      // Rich center-led speech cadence in emerald green
      static const int SEQ[6][5] = {
        { 6, 12, 20, 12,  6},
        {10, 18, 24, 18, 10},
        { 8, 14, 16, 14,  8},
        {12, 20, 22, 20, 12},
        { 6, 14, 18, 14,  6},
        { 8, 16, 24, 16,  8},
      };
      const int *h = SEQ[f % 6];
      const uint32_t c[5] = {0x238636, 0x3FB950, 0x56D364, 0x3FB950, 0x238636};
      for (int i = 0; i < 5; i++) set_bar(ic.bars[i], i, h[i], c[i]);
      set_spark(ic, 7, 0x56D364, 0xE6FFED, LV_OPA_COVER);
      apply_btn_style(btn, 0x0C2518, 0x3FB950);
      break;
    }

    case MUTED: {
      // Collapsed flat baseline + crisp red slash
      for (int i = 0; i < 5; i++) set_bar(ic.bars[i], i, 4, 0x6E3630, LV_OPA_70);
      set_spark(ic, 5, 0x6E3630, 0x6E3630, LV_OPA_40);
      apply_btn_style(btn, 0x241115, 0xF85149);
      break;
    }

    case ERROR: {
      const bool flash = (f % 4) < 2;
      for (int i = 0; i < 5; i++) set_bar(ic.bars[i], i, 10, flash ? 0xF85149 : 0x8B3A3A);
      set_spark(ic, 5, 0xF85149, 0xFFDCE0, LV_OPA_COVER);
      apply_btn_style(btn, 0x241115, 0xF85149);
      break;
    }
  }
}

}  // namespace voice_icon
