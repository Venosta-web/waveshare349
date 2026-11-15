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

struct Stage {
  lv_obj_t *root = nullptr;
  lv_obj_t *outer_ring = nullptr;
  lv_obj_t *inner_ring = nullptr;
  lv_obj_t *bars[9] = {};
  lv_obj_t *spark_h = nullptr;
  lv_obj_t *spark_v = nullptr;
  lv_obj_t *spark_c = nullptr;
  lv_obj_t *slash[7] = {};
};

inline lv_obj_t *ring(lv_obj_t *parent, int size, int border_w) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, size, size);
  lv_obj_set_pos(o, (124 - size) / 2, (124 - size) / 2);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(o, border_w, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  return o;
}

inline void build_stage(Stage &st, lv_obj_t *root) {
  st.root = root;
  lv_obj_set_size(root, 124, 124);
  lv_obj_remove_flag(root, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

  st.outer_ring = ring(root, 114, 1);
  st.inner_ring = ring(root, 90, 2);

  for (int i = 0; i < 9; i++) {
    st.bars[i] = pill(root, 0x58A6FF);
  }

  st.spark_h = pill(root, 0x79C0FF);
  st.spark_v = pill(root, 0x79C0FF);
  st.spark_c = pill(root, 0xFFFFFF);

  for (int i = 0; i < 7; i++) {
    st.slash[i] = pill(root, 0xF85149);
    lv_obj_set_size(st.slash[i], 10, 3);
    lv_obj_set_pos(st.slash[i], 30 + i * 9, 36 + i * 8);
    lv_obj_add_flag(st.slash[i], LV_OBJ_FLAG_HIDDEN);
  }
}

inline void set_stage_bar(lv_obj_t *bar, int idx, int h, uint32_t color, lv_opa_t opa = LV_OPA_COVER) {
  const int x = 28 + idx * 8;  // 9 bars: 4px wide, 4px gap -> x = 28..92
  const int y_mid = 62;
  const int y = y_mid - h / 2;
  lv_obj_set_pos(bar, x, y);
  lv_obj_set_size(bar, 4, h);
  lv_obj_set_style_bg_color(bar, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(bar, opa, 0);
}

inline void set_stage_spark(Stage &st, int arm_len, uint32_t arm_col, uint32_t core_col, lv_opa_t opa) {
  const int cx = 94;
  const int cy = 28;
  lv_obj_set_pos(st.spark_h, cx - arm_len / 2, cy);
  lv_obj_set_size(st.spark_h, arm_len, 2);
  lv_obj_set_style_bg_color(st.spark_h, lv_color_hex(arm_col), 0);
  lv_obj_set_style_bg_opa(st.spark_h, opa, 0);

  lv_obj_set_pos(st.spark_v, cx, cy - arm_len / 2);
  lv_obj_set_size(st.spark_v, 2, arm_len);
  lv_obj_set_style_bg_color(st.spark_v, lv_color_hex(arm_col), 0);
  lv_obj_set_style_bg_opa(st.spark_v, opa, 0);

  lv_obj_set_pos(st.spark_c, cx - 1, cy - 1);
  lv_obj_set_size(st.spark_c, 4, 4);
  lv_obj_set_style_bg_color(st.spark_c, lv_color_hex(core_col), 0);
  lv_obj_set_style_bg_opa(st.spark_c, opa, 0);
}

inline void style_stage_rings(Stage &st, int outer_sz, uint32_t outer_col, lv_opa_t outer_opa,
                              uint32_t inner_col, uint32_t inner_bg, lv_opa_t inner_bg_opa) {
  lv_obj_set_size(st.outer_ring, outer_sz, outer_sz);
  lv_obj_set_pos(st.outer_ring, (124 - outer_sz) / 2, (124 - outer_sz) / 2);
  lv_obj_set_style_border_color(st.outer_ring, lv_color_hex(outer_col), 0);
  lv_obj_set_style_border_opa(st.outer_ring, outer_opa, 0);

  lv_obj_set_style_border_color(st.inner_ring, lv_color_hex(inner_col), 0);
  lv_obj_set_style_bg_color(st.inner_ring, lv_color_hex(inner_bg), 0);
  lv_obj_set_style_bg_opa(st.inner_ring, inner_bg_opa, 0);
}

inline void tick_stage(lv_obj_t *box) {
  if (!box) return;
  static Stage st;
  if (st.root == nullptr) build_stage(st, box);

  const uint32_t f = g_frame;
  const bool muted = (g_mode == MUTED);

  for (int i = 0; i < 7; i++) {
    if (muted) lv_obj_clear_flag(st.slash[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(st.slash[i], LV_OBJ_FLAG_HIDDEN);
  }

  switch (g_mode) {
    case IDLE: {
      const int h[9] = {8, 12, 18, 24, 30, 24, 18, 12, 8};
      const uint32_t c[9] = {0x2B5B84, 0x2B5B84, 0x388BFD, 0x58A6FF, 0x79C0FF, 0x58A6FF, 0x388BFD, 0x2B5B84, 0x2B5B84};
      for (int i = 0; i < 9; i++) set_stage_bar(st.bars[i], i, h[i], c[i]);
      set_stage_spark(st, 9, 0x79C0FF, 0xE6EDF3, LV_OPA_COVER);
      style_stage_rings(st, 110, 0x26466D, LV_OPA_50, 0x388BFD, 0x0F2744, LV_OPA_20);
      break;
    }

    case LISTENING: {
      static const int SEQ[8][9] = {
        {10, 18, 28, 38, 44, 34, 24, 16, 10},
        {14, 24, 34, 28, 40, 42, 30, 20, 12},
        { 8, 16, 30, 42, 48, 38, 28, 18, 12},
        {12, 26, 38, 32, 36, 26, 20, 14,  8},
        {16, 22, 28, 38, 44, 40, 32, 22, 14},
        {10, 16, 26, 44, 46, 32, 26, 20, 14},
        {12, 22, 32, 28, 38, 36, 26, 16, 10},
        { 8, 24, 36, 40, 42, 30, 22, 14, 10},
      };
      static const int PULSE[8] = {108, 112, 116, 120, 118, 114, 110, 108};
      const int *h = SEQ[f % 8];
      const uint32_t c[9] = {0x2B5B84, 0x388BFD, 0x58A6FF, 0x79C0FF, 0xA5D6FF, 0x79C0FF, 0x58A6FF, 0x388BFD, 0x2B5B84};
      for (int i = 0; i < 9; i++) set_stage_bar(st.bars[i], i, h[i], c[i]);
      set_stage_spark(st, (f % 4) < 2 ? 13 : 9, 0xA5D6FF, 0xFFFFFF, LV_OPA_COVER);
      style_stage_rings(st, PULSE[f % 8], 0x58A6FF, LV_OPA_70, 0x58A6FF, 0x102A4A, LV_OPA_30);
      break;
    }

    case THINKING: {
      const int active = (f / 2) % 9;
      for (int i = 0; i < 9; i++) {
        const int dist = i > active ? i - active : active - i;
        const int h = dist == 0 ? 40 : (dist == 1 ? 28 : (dist == 2 ? 18 : 10));
        const uint32_t col = dist == 0 ? 0xFFF3BF : (dist == 1 ? 0xFFD166 : (dist == 2 ? 0xFFB020 : 0x7A5610));
        set_stage_bar(st.bars[i], i, h, col);
      }
      set_stage_spark(st, (f % 4) < 2 ? 13 : 9, 0xFFD166, 0xFFF3BF, LV_OPA_COVER);
      const int outer = (f % 6) < 3 ? 114 : 108;
      style_stage_rings(st, outer, 0xFFB020, LV_OPA_60, 0xFFB020, 0x2E2006, LV_OPA_30);
      break;
    }

    case SPEAKING: {
      static const int SEQ[6][9] = {
        { 8, 14, 24, 34, 42, 34, 24, 14,  8},
        {12, 22, 32, 42, 48, 42, 32, 22, 12},
        {10, 18, 26, 32, 36, 32, 26, 18, 10},
        {14, 24, 36, 40, 46, 40, 36, 24, 14},
        { 8, 16, 26, 36, 40, 36, 26, 16,  8},
        {10, 20, 32, 42, 48, 42, 32, 20, 10},
      };
      const int *h = SEQ[f % 6];
      const uint32_t c[9] = {0x1B5E20, 0x238636, 0x3FB950, 0x56D364, 0x7EE787, 0x56D364, 0x3FB950, 0x238636, 0x1B5E20};
      for (int i = 0; i < 9; i++) set_stage_bar(st.bars[i], i, h[i], c[i]);
      set_stage_spark(st, (f % 4) < 2 ? 13 : 11, 0x56D364, 0xE6FFED, LV_OPA_COVER);
      const int outer = (f % 4) < 2 ? 116 : 110;
      style_stage_rings(st, outer, 0x3FB950, LV_OPA_70, 0x3FB950, 0x0C2518, LV_OPA_30);
      break;
    }

    case MUTED: {
      for (int i = 0; i < 9; i++) set_stage_bar(st.bars[i], i, 6, 0x6E3630, LV_OPA_70);
      set_stage_spark(st, 7, 0x6E3630, 0x6E3630, LV_OPA_40);
      style_stage_rings(st, 108, 0xF85149, LV_OPA_40, 0xF85149, 0x241115, LV_OPA_30);
      break;
    }

    case ERROR: {
      const bool flash = (f % 4) < 2;
      for (int i = 0; i < 9; i++) set_stage_bar(st.bars[i], i, 16, flash ? 0xF85149 : 0x8B3A3A);
      set_stage_spark(st, 9, 0xF85149, 0xFFDCE0, LV_OPA_COVER);
      style_stage_rings(st, flash ? 114 : 108, 0xF85149, LV_OPA_70, 0xF85149, 0x241115, LV_OPA_30);
      break;
    }
  }
}

}  // namespace voice_icon
