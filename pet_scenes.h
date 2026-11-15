#pragma once
// Additional 150x140 activity-stage dioramas shared by Clawd (Claude) and Byte (Codex).
// Each scene builds its own LVGL pixel-art props inside `stage`, positions `pet_box`
// and `bubble` in the right z-order, and returns a `PoseOffset` to layer onto the
// mascot's base animation at 10 Hz.
#include <lvgl.h>
#include <cstdint>
#include <ctime>

namespace pet_scenes {

enum PetMode : uint8_t {
  PM_IDLE = 0,
  PM_WORKING,
  PM_PENDING,
  PM_NEEDS_INPUT,
  PM_DONE,
  PM_SENT_OK,
  PM_SENT_NO,
  PM_RATE_LIMITED,
};

struct PoseOffset {
  int dx = 0, dy = 0;
  int arm_l = 0, arm_r = 0;
  int eye_dx = 0;
  int eye_h = -1;  // >= 0 overrides eye height
  bool override_arms = false;
  bool override_eye_dx = false;
};

inline lv_obj_t *blk(lv_obj_t *parent, uint32_t color, lv_opa_t opa = LV_OPA_COVER, int radius = 0) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_remove_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  return o;
}

inline void place(lv_obj_t *o, int x, int y, int w, int h) {
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
}

inline void set_vis(lv_obj_t *o, bool visible) {
  if (visible) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

template <typename PoseT>
inline void merge_pose(PoseT &s, const PoseOffset &po) {
  s.dx += po.dx;
  s.dy += po.dy;
  if (po.override_arms) {
    s.arm_l = po.arm_l;
    s.arm_r = po.arm_r;
  } else {
    s.arm_l += po.arm_l;
    s.arm_r += po.arm_r;
  }
  if (po.override_eye_dx) s.eye_dx = po.eye_dx;
  else s.eye_dx += po.eye_dx;
  if (po.eye_h >= 0) s.eye_h = po.eye_h;
}

// ── 1. Moonlit Pier & Bobber (Fishing) ──────────────────────────────────────
struct FishingScene {
  lv_obj_t *stage = nullptr;
  lv_obj_t *stars[3] = {};
  lv_obj_t *waves[3] = {};
  lv_obj_t *rod[3] = {};
  lv_obj_t *line = nullptr;
  lv_obj_t *bob_top = nullptr, *bob_bot = nullptr;
  lv_obj_t *splash[3] = {};
  lv_obj_t *fish_body = nullptr, *fish_tail = nullptr, *fish_eye = nullptr;
  bool is_codex = false;

  void build(lv_obj_t *s, lv_obj_t *pet_box, lv_obj_t *bubble, bool codex) {
    stage = s;
    is_codex = codex;
    lv_obj_set_style_bg_color(stage, lv_color_hex(0x09111E), 0);

    // Crescent moon
    lv_obj_t *moon = blk(stage, 0xE6EDF3, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    place(moon, 126, 10, 12, 12);
    lv_obj_t *cut = blk(stage, 0x09111E, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    place(cut, 122, 8, 10, 10);

    // Twinkling stars
    static const int SX[3] = {20, 62, 96}, SY[3] = {14, 24, 12};
    for (int i = 0; i < 3; i++) {
      stars[i] = blk(stage, 0x8B949E);
      place(stars[i], SX[i], SY[i], 2, 2);
    }

    // Water & surface highlight
    place(blk(stage, 0x0E2A47), 0, 102, 150, 38);
    place(blk(stage, 0x1F5685), 0, 102, 150, 2);
    static const int WY[3] = {110, 120, 130}, WW[3] = {22, 16, 26};
    for (int i = 0; i < 3; i++) {
      waves[i] = blk(stage, i == 1 ? 0x2B6CB0 : 0x1E4E79, LV_OPA_80, 1);
      place(waves[i], 10 + i * 40, WY[i], WW[i], 2);
    }

    // Wooden pier & pilings
    place(blk(stage, 0x4A311D), 12, 102, 6, 38);
    place(blk(stage, 0x4A311D), 48, 102, 6, 38);
    place(blk(stage, 0x6E4B2E), 0, 96, 66, 6);
    place(blk(stage, 0x8B6240), 0, 96, 66, 2);

    // Mascot stands on the pier (u = 5 -> feet at +45 px, 51 + 45 = 96)
    lv_obj_set_pos(pet_box, -2, 51);
    lv_obj_move_foreground(pet_box);
    lv_obj_set_pos(bubble, 82, 10);
    lv_obj_move_foreground(bubble);

    // Foreground fishing gear
    for (int i = 0; i < 3; i++) rod[i] = blk(stage, i == 2 ? 0xC9D1D9 : 0xA67C52);
    line = blk(stage, 0xC9D1D9, LV_OPA_70);
    bob_top = blk(stage, 0xF85149, LV_OPA_COVER, 1);
    bob_bot = blk(stage, 0xF0F6FC, LV_OPA_COVER, 1);
    for (int i = 0; i < 3; i++) {
      splash[i] = blk(stage, 0x79C0FF, LV_OPA_COVER, 1);
      set_vis(splash[i], false);
    }

    uint32_t fish_col = codex ? 0x34D399 : 0xFFB020;
    fish_body = blk(stage, fish_col, LV_OPA_COVER, 2);
    fish_tail = blk(stage, fish_col);
    fish_eye = blk(stage, 0x0A0C10);
    set_vis(fish_body, false);
    set_vis(fish_tail, false);
    set_vis(fish_eye, false);
  }

  PoseOffset tick(PetMode m, uint32_t f, uint32_t t) {
    PoseOffset po;
    // Stars twinkle
    for (int i = 0; i < 3; i++) {
      bool bright = ((f / 7 + i * 3) % 5) == 0;
      lv_obj_set_style_bg_color(stars[i], lv_color_hex(bright ? 0xF0F6FC : 0x6E7681), 0);
    }

    // Drifting water shimmer
    int speed = (m == PM_WORKING) ? 2 : 1;
    int w0 = ((int) (f * speed) * 2) % 150;
    int w1 = 140 - (((int) (f * speed) * 3) % 150);
    int w2 = ((int) (f * speed) + 45) % 150;
    lv_obj_set_x(waves[0], w0);
    lv_obj_set_x(waves[1], w1);
    lv_obj_set_x(waves[2], w2);

    // Rod & bobber kinematics
    int tip_dy = 0;
    int bob_x = 104, bob_y = 100;
    bool show_splash = false;

    if (m == PM_IDLE || m == PM_RATE_LIMITED) {
      tip_dy = (m == PM_RATE_LIMITED) ? 4 : ((f / 14) % 2);
      bob_y = 100 + ((f / 10) % 2);
    } else if (m == PM_WORKING) {
      uint32_t cyc = f % 32;
      bool nibble = (cyc >= 20 && cyc <= 27);
      if (nibble) {
        tip_dy = (cyc % 2) ? 5 : 2;
        bob_y = 103 + (int) (cyc % 3);
        show_splash = true;
        po.dx = (cyc % 2) ? -1 : 1;
        po.arm_r = -4;
        po.override_arms = true;
      } else {
        tip_dy = (cyc < 4) ? -3 : ((f / 8) % 2);
        bob_y = 99 + ((f / 5) % 3);
      }
      po.eye_dx = 2;
      po.override_eye_dx = true;
    } else if (m == PM_PENDING) {
      tip_dy = (f % 2) ? -5 : 4;
      bob_y = 104 + (f % 3);
      bob_x = 102 + (f % 4);
      show_splash = true;
    } else if (m == PM_DONE || m == PM_SENT_OK) {
      tip_dy = -6;
      bob_y = 92;
      show_splash = (t < 14);
    } else if (m == PM_SENT_NO) {
      tip_dy = 6;
      bob_y = 101;
    }

    // Stepped fishing rod from mascot's right hand (60, 74) to (104, 46 + tip_dy)
    place(rod[0], 58, 71 + tip_dy / 3, 16, 3);
    place(rod[1], 71, 61 + tip_dy / 2, 16, 3);
    place(rod[2], 84, 51 + tip_dy, 20, 2);

    int lx = 103;
    int ly = 52 + tip_dy;
    int lh = bob_y - ly;
    if (lh < 4) lh = 4;
    place(line, lx, ly, 1, lh);
    place(bob_top, bob_x - 2, bob_y, 6, 3);
    place(bob_bot, bob_x - 2, bob_y + 3, 6, 3);

    // Splash droplets
    for (int i = 0; i < 3; i++) {
      set_vis(splash[i], show_splash);
      if (show_splash) {
        int spread = (int) (f % 4) * 2;
        int sx = bob_x + (i - 1) * (5 + spread);
        int sy = 96 - (i == 1 ? 4 : 1) - (f % 2) * 3;
        place(splash[i], sx, sy, 3, 3);
      }
    }

    // Leaping fish on Done / Sent OK
    bool show_fish = (m == PM_DONE || m == PM_SENT_OK) && t < 24;
    set_vis(fish_body, show_fish);
    set_vis(fish_tail, show_fish);
    set_vis(fish_eye, show_fish);
    if (show_fish) {
      int k = (int) (t % 16);
      int arc = k < 8 ? k : (15 - k);
      int fx = 114;
      int fy = 102 - arc * 5;
      place(fish_body, fx, fy, 11, 7);
      place(fish_tail, fx + 10, fy + 1, 4, 5);
      place(fish_eye, fx + 2, fy + 2, 2, 2);
    }

    return po;
  }
};

// ── 2. Cavern Ore & Pickaxe (Mining) ────────────────────────────────────────
struct MiningScene {
  lv_obj_t *stage = nullptr;
  lv_obj_t *lantern_glow = nullptr, *lantern_core = nullptr;
  lv_obj_t *ore[3] = {};
  lv_obj_t *helmet = nullptr, *headlamp = nullptr;
  lv_obj_t *pick_handle[2] = {}, *pick_head = nullptr, *pick_tip = nullptr;
  lv_obj_t *sparks[4] = {};
  lv_obj_t *gem[2] = {};
  bool is_codex = false;

  void build(lv_obj_t *s, lv_obj_t *pet_box, lv_obj_t *bubble, bool codex) {
    stage = s;
    is_codex = codex;
    lv_obj_set_style_bg_color(stage, lv_color_hex(0x0B0D12), 0);

    // Timber beam & hanging lantern
    place(blk(stage, 0x3D291A), 6, 8, 6, 110);
    place(blk(stage, 0x4A311D), 0, 0, 44, 8);
    lantern_glow = blk(stage, 0xFFB020, LV_OPA_20, LV_RADIUS_CIRCLE);
    place(lantern_glow, 16, 10, 26, 26);
    place(blk(stage, 0x6E7681), 28, 8, 2, 8);
    place(blk(stage, 0x30363D), 25, 16, 8, 10);
    lantern_core = blk(stage, 0xFFB020, LV_OPA_COVER, 1);
    place(lantern_core, 27, 18, 4, 6);

    // Cavern floor & stepped rock wall
    place(blk(stage, 0x1C2128), 0, 118, 150, 22);
    place(blk(stage, 0x2D333B), 10, 124, 14, 3);
    place(blk(stage, 0x2D333B), 76, 128, 18, 3);

    place(blk(stage, 0x161B22), 92, 12, 58, 18);
    place(blk(stage, 0x21262D), 102, 26, 48, 92);
    place(blk(stage, 0x30363D), 102, 26, 3, 92);

    // Glowing ore veins
    uint32_t c0 = codex ? 0x10B981 : 0xFFB020;
    uint32_t c1 = codex ? 0xA7F3D0 : 0xD97757;
    uint32_t c2 = 0x58A6FF;
    ore[0] = blk(stage, c0, LV_OPA_COVER, 1);
    ore[1] = blk(stage, c1, LV_OPA_COVER, 1);
    ore[2] = blk(stage, c2, LV_OPA_COVER, 1);
    place(ore[0], 110, 44, 10, 6);
    place(ore[1], 124, 66, 12, 8);
    place(ore[2], 108, 88, 9, 6);

    // Mascot at (20, 73), u = 5 -> feet at 73 + 45 = 118
    lv_obj_set_pos(pet_box, 20, 73);
    lv_obj_move_foreground(pet_box);
    lv_obj_set_pos(bubble, 76, 8);
    lv_obj_move_foreground(bubble);

    // Mining helmet + headlamp
    helmet = blk(stage, 0xFFB020, LV_OPA_COVER, 2);
    headlamp = blk(stage, 0xFFF3C4, LV_OPA_COVER, 1);

    // Pickaxe & sparks
    pick_handle[0] = blk(stage, 0x8B6F4E);
    pick_handle[1] = blk(stage, 0x8B6F4E);
    pick_head = blk(stage, 0xC9D1D9);
    pick_tip = blk(stage, 0xF0F6FC);
    for (int i = 0; i < 4; i++) {
      sparks[i] = blk(stage, i % 2 ? 0xFFB020 : 0xF0F6FC);
      set_vis(sparks[i], false);
    }

    // Jackpot gem
    gem[0] = blk(stage, 0x58A6FF, LV_OPA_COVER, 2);
    gem[1] = blk(stage, 0xA5D6FF, LV_OPA_COVER, 1);
    set_vis(gem[0], false);
    set_vis(gem[1], false);
  }

  PoseOffset tick(PetMode m, uint32_t f, uint32_t t) {
    PoseOffset po;
    // Lantern pulse
    lv_opa_t glow_opa = (m == PM_IDLE || m == PM_RATE_LIMITED) ? LV_OPA_10
                      : ((f / 4) % 2 ? LV_OPA_30 : LV_OPA_20);
    lv_obj_set_style_bg_opa(lantern_glow, glow_opa, 0);
    uint32_t lcol = (m == PM_PENDING && (f / 3) % 2) ? 0xFFE082
                  : (m == PM_RATE_LIMITED) ? 0x8A5D12 : 0xFFB020;
    lv_obj_set_style_bg_color(lantern_core, lv_color_hex(lcol), 0);

    bool impact = false;
    int swing_phase = 0;  // 0 = rest, 1 = windup, 2 = strike
    if (m == PM_WORKING) {
      int k = (int) (f % 8);
      if (k < 3) {
        swing_phase = 1;
        po.arm_r = -14;
        po.override_arms = true;
      } else if (k <= 5) {
        swing_phase = 2;
        po.dx = 4;
        po.arm_r = 2;
        po.override_arms = true;
        impact = (k == 3 || k == 4);
      } else {
        swing_phase = 0;
        po.dx = 1;
      }
      po.eye_dx = 2;
      po.override_eye_dx = true;
    } else if (m == PM_PENDING) {
      swing_phase = (f / 3) % 2 ? 1 : 0;
    } else if (m == PM_DONE || m == PM_SENT_OK) {
      swing_phase = 1;
    }

    // Helmet tracks mascot head (pet_box at 20,73; body top at +10 = y 83, x 20+10 = 30)
    int hx = 20 + 20 + po.dx;
    int hy = 73 + 6 + po.dy;
    if (m == PM_DONE && t < 9) {
      static const int HOP[] = {0, -1, -2, -3, -3, -2, -1, 0, 0};
      hy += HOP[t] * 5;
    }
    place(helmet, hx, hy, 28, 6);
    place(headlamp, hx + 22, hy + 1, 5, 4);

    // Pickaxe position
    if (swing_phase == 1) {
      place(pick_handle[0], 78 + po.dx, 70 + po.dy, 3, 14);
      place(pick_handle[1], 76 + po.dx, 58 + po.dy, 3, 14);
      place(pick_head, 68 + po.dx, 54 + po.dy, 16, 4);
      place(pick_tip, 82 + po.dx, 56 + po.dy, 4, 4);
    } else if (swing_phase == 2) {
      place(pick_handle[0], 82 + po.dx, 84, 14, 3);
      place(pick_handle[1], 92 + po.dx, 80, 10, 3);
      place(pick_head, 99, 72, 4, 16);
      place(pick_tip, 101, 76, 4, 4);
    } else {
      place(pick_handle[0], 80 + po.dx, 86, 12, 3);
      place(pick_handle[1], 88 + po.dx, 88, 8, 3);
      place(pick_head, 93, 82, 4, 14);
      place(pick_tip, 95, 92, 4, 4);
    }

    // Ore flash & sparks on strike
    lv_obj_set_style_bg_color(ore[1], lv_color_hex(impact ? 0xFFF3C4 : (is_codex ? 0xA7F3D0 : 0xD97757)), 0);
    for (int i = 0; i < 4; i++) {
      set_vis(sparks[i], impact);
      if (impact) {
        int age = (int) (f % 2);
        int sx = 98 - (i + 1) * 4 - age * 4;
        int sy = 76 + (i - 1) * 5 + age * 3;
        place(sparks[i], sx, sy, 3, 3);
      }
    }

    // Jackpot gem on Done
    bool show_gem = (m == PM_DONE || m == PM_SENT_OK) && t < 24;
    set_vis(gem[0], show_gem);
    set_vis(gem[1], show_gem);
    if (show_gem) {
      int lift = (t < 10) ? (int) t * 3 : 30;
      place(gem[0], 110, 56 - lift, 12, 12);
      place(gem[1], 113, 59 - lift, 5, 5);
    }
    return po;
  }
};

// ── 3. Table Tennis Rally (Ping Pong) ───────────────────────────────────────
struct PingPongScene {
  lv_obj_t *stage = nullptr;
  lv_obj_t *paddle_face = nullptr, *paddle_handle = nullptr;
  lv_obj_t *rival_body = nullptr, *rival_face = nullptr, *rival_eye = nullptr;
  lv_obj_t *ball = nullptr, *ball_shadow = nullptr;
  bool is_codex = false;

  void build(lv_obj_t *s, lv_obj_t *pet_box, lv_obj_t *bubble, bool codex) {
    stage = s;
    is_codex = codex;
    lv_obj_set_style_bg_color(stage, lv_color_hex(0x0D1117), 0);

    // Arena floor
    place(blk(stage, 0x131922), 0, 118, 150, 22);

    // Ping-pong table + net (positioned cleanly to the right of the mascot)
    place(blk(stage, 0x30363D), 50, 94, 4, 24);
    place(blk(stage, 0x30363D), 116, 94, 4, 24);
    place(blk(stage, 0x1F6FEB), 42, 88, 86, 6);
    place(blk(stage, 0xF0F6FC), 42, 88, 86, 2);
    place(blk(stage, 0x8B949E), 84, 78, 2, 10);
    place(blk(stage, 0xF0F6FC), 83, 78, 4, 2);

    // Mascot at (0, 77), u = 4 -> feet at 77 + 36 = 113 (meets floor comfortably)
    lv_obj_set_pos(pet_box, 0, 77);
    lv_obj_move_foreground(pet_box);
    lv_obj_set_pos(bubble, 62, 10);
    lv_obj_move_foreground(bubble);

    // Rival mini-paddle bot on the right
    rival_body = blk(stage, 0x21262D, LV_OPA_COVER, 2);
    rival_face = blk(stage, codex ? 0xD97757 : 0x10B981, LV_OPA_COVER, 1);
    rival_eye = blk(stage, 0xF0F6FC);

    // Mascot's paddle
    paddle_handle = blk(stage, 0x8B6F4E);
    paddle_face = blk(stage, codex ? 0x10B981 : 0xF85149, LV_OPA_COVER, 2);

    // Ping-pong ball & table shadow
    ball_shadow = blk(stage, 0x000000, LV_OPA_40, LV_RADIUS_CIRCLE);
    ball = blk(stage, 0xFFFFFF, LV_OPA_COVER, LV_RADIUS_CIRCLE);
  }

  PoseOffset tick(PetMode m, uint32_t f, uint32_t t) {
    PoseOffset po;
    int bx = 74, by = 83;
    int rival_x = 132, rival_y = 74;
    int pad_x = 48, pad_y = 84;

    if (m == PM_WORKING) {
      // 16-frame crisp rally over the net
      static const int BX[16] = {52, 62, 72, 83, 94, 106, 117, 126, 124, 114, 103, 91, 80, 68, 57, 50};
      static const int BY[16] = {74, 68, 65, 66, 72, 83, 76, 72, 71, 66, 64, 67, 73, 83, 78, 75};
      int k = (int) (f % 16);
      bx = BX[k];
      by = BY[k];
      rival_y = by - 5;
      if (k >= 6 && k <= 9) rival_x = 129;

      bool hit_left = (k == 15 || k == 0 || k == 1);
      if (hit_left) {
        po.dx = 2;
        po.arm_r = -6;
        po.override_arms = true;
        pad_x = 50;
        pad_y = 76;
      } else {
        pad_y = 82 + (k % 2);
      }
      po.eye_dx = (bx > 84) ? 2 : 0;
      po.override_eye_dx = true;
    } else if (m == PM_PENDING) {
      bx = 74;
      by = 62 + ((f / 2) % 2) * 3;
      pad_x = 46;
      pad_y = ((f / 3) % 2) ? 68 : 82;
    } else if (m == PM_NEEDS_INPUT) {
      // Bounce ball gently on paddle
      static const int JUG[6] = {0, -6, -10, -11, -7, -2};
      bx = 49;
      by = 76 + JUG[f % 6];
      pad_x = 48;
      pad_y = (f % 6 == 0) ? 81 : 83;
    } else if (m == PM_DONE || m == PM_SENT_OK) {
      if (t < 10) {
        bx = 56 + (int) t * 9;
        by = 76 - (int) (t < 5 ? t * 2 : (10 - t));
      } else {
        bx = 136;
        by = 113;
      }
      pad_x = 48;
      pad_y = 66;
    } else if (m == PM_SENT_NO) {
      bx = 34;
      by = 113;
      pad_x = 46;
      pad_y = 90;
    } else {
      // IDLE / RATE_LIMITED
      bx = 62;
      by = 83;
      pad_x = 46;
      pad_y = 88;
    }

    place(paddle_handle, pad_x, pad_y + 8, 3, 6);
    place(paddle_face, pad_x - 2, pad_y, 7, 10);

    place(rival_body, rival_x, rival_y, 10, 18);
    place(rival_face, rival_x - 2, rival_y + 2, 3, 14);
    place(rival_eye, rival_x + 4, rival_y + 4, 2, 2);

    bool on_table = (bx >= 42 && bx <= 126 && by <= 90);
    set_vis(ball_shadow, on_table);
    if (on_table) place(ball_shadow, bx, 87, 5, 2);
    place(ball, bx, by, 5, 5);

    return po;
  }
};

// ── 4. Night Campfire & Embers (Campfire) ───────────────────────────────────
struct CampfireScene {
  lv_obj_t *stage = nullptr;
  lv_obj_t *stars[4] = {};
  lv_obj_t *glow = nullptr;
  lv_obj_t *flame_out = nullptr, *flame_mid = nullptr, *flame_core = nullptr;
  lv_obj_t *embers[3] = {};
  lv_obj_t *stick = nullptr, *mallow = nullptr;

  void build(lv_obj_t *s, lv_obj_t *pet_box, lv_obj_t *bubble, bool /*codex*/) {
    stage = s;
    lv_obj_set_style_bg_color(stage, lv_color_hex(0x080C14), 0);

    static const int SX[4] = {18, 52, 92, 130}, SY[4] = {16, 10, 20, 12};
    for (int i = 0; i < 4; i++) {
      stars[i] = blk(stage, 0x8B949E);
      place(stars[i], SX[i], SY[i], 2, 2);
    }

    // Ground & warm campfire glow
    place(blk(stage, 0x131922), 0, 118, 150, 22);
    glow = blk(stage, 0x4A2612, LV_OPA_70, LV_RADIUS_CIRCLE);
    place(glow, 82, 112, 54, 12);

    // Crossed logs
    place(blk(stage, 0x5A3E28), 94, 112, 28, 5);
    place(blk(stage, 0x3D291A), 98, 110, 20, 4);

    // Mascot at (4, 73), u = 5 -> feet at 73 + 45 = 118
    lv_obj_set_pos(pet_box, 4, 73);
    lv_obj_move_foreground(pet_box);
    lv_obj_set_pos(bubble, 78, 10);
    lv_obj_move_foreground(bubble);

    // Roasting stick & marshmallow
    stick = blk(stage, 0x8B6F4E);
    mallow = blk(stage, 0xF0F6FC, LV_OPA_COVER, 1);

    // 3-layer flame & rising embers
    flame_out = blk(stage, 0xF85149, LV_OPA_COVER, 4);
    flame_mid = blk(stage, 0xFFB020, LV_OPA_COVER, 3);
    flame_core = blk(stage, 0xFFE082, LV_OPA_COVER, 2);
    for (int i = 0; i < 3; i++) embers[i] = blk(stage, i == 1 ? 0xF85149 : 0xFFB020);
  }

  PoseOffset tick(PetMode m, uint32_t f, uint32_t t) {
    PoseOffset po;
    for (int i = 0; i < 4; i++) {
      bool bright = ((f / 6 + i * 2) % 5) == 0;
      lv_obj_set_style_bg_color(stars[i], lv_color_hex(bright ? 0xF0F6FC : 0x6E7681), 0);
    }

    bool low_fire = (m == PM_IDLE || m == PM_RATE_LIMITED);
    int flicker = (int) (f % 4);
    int oh = low_fire ? 7 + (flicker % 2) : 20 + (flicker == 1 ? 4 : flicker == 3 ? -2 : 0);
    int mh = low_fire ? 4 : 13 + ((flicker + 1) % 3) * 2;
    int ch = low_fire ? 2 : 7 + (flicker % 2) * 2;
    int fx_jit = low_fire ? 0 : (flicker == 2 ? 1 : flicker == 0 ? -1 : 0);

    place(flame_out, 100 + fx_jit, 112 - oh, 18, oh);
    place(flame_mid, 103 - fx_jit, 112 - mh, 12, mh);
    place(flame_core, 106, 112 - ch, 6, ch);
    lv_obj_set_style_bg_opa(glow, low_fire ? LV_OPA_30 : (flicker % 2 ? LV_OPA_80 : LV_OPA_60), 0);

    // Rising embers
    for (int i = 0; i < 3; i++) {
      if (low_fire) {
        set_vis(embers[i], false);
      } else {
        set_vis(embers[i], true);
        int age = (int) ((f + i * 6) % 18);
        int ex = 104 + i * 4 + ((age / 3) % 2 ? 2 : -2);
        int ey = 92 - age * 3;
        place(embers[i], ex, ey, 2, 2);
        lv_obj_set_style_bg_opa(embers[i], age > 13 ? LV_OPA_30 : LV_OPA_COVER, 0);
      }
    }

    // Roasting stick & marshmallow
    int stick_y = 95;
    if (m == PM_WORKING) {
      stick_y = 94 + ((f / 4) % 2 ? -2 : 1);
      po.arm_r = -4;
      po.override_arms = true;
      po.eye_dx = 2;
      po.override_eye_dx = true;
    } else if (m == PM_DONE || m == PM_SENT_OK) {
      stick_y = (t < 8) ? 84 : 93;
    } else if (m == PM_RATE_LIMITED || m == PM_IDLE) {
      stick_y = 99;
    }

    uint32_t m_col = (m == PM_DONE || m == PM_SENT_OK) ? 0xE39B4B
                   : (m == PM_SENT_NO) ? 0x30363D : 0xF0F6FC;
    lv_obj_set_style_bg_color(mallow, lv_color_hex(m_col), 0);
    place(stick, 66 + po.dx, stick_y + po.dy, 28, 2);
    place(mallow, 92 + po.dx, stick_y - 2 + po.dy, 6, 6);

    return po;
  }
};

// ── Terminal Floor Variants (540x46 floor strip, u = 4, feet at y = 137) ───

struct TerminalFishingScene {
  lv_obj_t *pier[3] = {};
  lv_obj_t *water = nullptr, *surf = nullptr, *waves[4] = {};
  lv_obj_t *rod[3] = {}, *line = nullptr, *bob_top = nullptr, *bob_bot = nullptr;
  lv_obj_t *splash[3] = {}, *fish_body = nullptr, *fish_tail = nullptr;
  bool is_codex = false;

  void build(lv_obj_t *card, lv_obj_t *pet_box, lv_obj_t *bubble, bool codex) {
    is_codex = codex;
    water = blk(card, 0x0E2A47);
    place(water, 118, 134, 422, 6);
    surf = blk(card, 0x1F5685);
    place(surf, 118, 134, 422, 2);
    for (int i = 0; i < 4; i++) {
      waves[i] = blk(card, 0x388BFD, LV_OPA_70, 1);
      place(waves[i], 140 + i * 95, 136 + (i % 2) * 2, 26, 2);
    }
    pier[0] = blk(card, 0x6E4B2E); place(pier[0], 0, 133, 122, 4);
    pier[1] = blk(card, 0x4A311D); place(pier[1], 18, 137, 5, 3);
    pier[2] = blk(card, 0x4A311D); place(pier[2], 96, 137, 5, 3);

    lv_obj_move_foreground(pet_box);
    lv_obj_move_foreground(bubble);

    for (int i = 0; i < 3; i++) rod[i] = blk(card, i == 2 ? 0xC9D1D9 : 0xA67C52);
    line = blk(card, 0xC9D1D9, LV_OPA_70);
    bob_top = blk(card, 0xF85149, LV_OPA_COVER, 1);
    bob_bot = blk(card, 0xF0F6FC, LV_OPA_COVER, 1);
    for (int i = 0; i < 3; i++) splash[i] = blk(card, 0x79C0FF, LV_OPA_COVER, 1);
    uint32_t fc = codex ? 0x34D399 : 0xFFB020;
    fish_body = blk(card, fc, LV_OPA_COVER, 2);
    fish_tail = blk(card, fc);
  }

  PoseOffset tick(PetMode m, uint32_t f, uint32_t t, int &pet_x) {
    PoseOffset po;
    const bool obstructed = (m == PM_PENDING || m == PM_RATE_LIMITED);
    set_vis(water, !obstructed);
    set_vis(surf, !obstructed);
    for (auto *w : waves) set_vis(w, !obstructed);
    for (auto *p : pier) set_vis(p, !obstructed);
    for (auto *r : rod) set_vis(r, !obstructed);
    set_vis(line, !obstructed);
    set_vis(bob_top, !obstructed);
    set_vis(bob_bot, !obstructed);

    if (obstructed) {
      pet_x = 242;
      for (auto *sp : splash) set_vis(sp, false);
      set_vis(fish_body, false);
      set_vis(fish_tail, false);
      return po;
    }

    pet_x = 58;
    int spd = (m == PM_WORKING) ? 3 : 1;
    for (int i = 0; i < 4; i++) {
      int wx = 122 + (int) ((f * spd * (i % 2 ? 2 : 3) + i * 95) % 390);
      lv_obj_set_x(waves[i], wx);
    }

    int tip_dy = 0, bob_y = 132;
    bool show_sp = false;
    if (m == PM_WORKING) {
      uint32_t cyc = f % 32;
      if (cyc >= 20 && cyc <= 27) {
        tip_dy = (cyc % 2) ? 4 : 1;
        bob_y = 134 + (int) (cyc % 2);
        show_sp = true;
        po.dx = (cyc % 2) ? -1 : 1;
        po.arm_r = -3;
        po.override_arms = true;
      } else {
        tip_dy = (cyc < 4) ? -3 : ((f / 8) % 2);
        bob_y = 131 + ((f / 5) % 2);
      }
      po.eye_dx = 2;
      po.override_eye_dx = true;
    } else if (m == PM_DONE || m == PM_SENT_OK) {
      tip_dy = -5;
      bob_y = 126;
      show_sp = (t < 14);
    }

    place(rod[0], 106, 118 + tip_dy / 3, 18, 2);
    place(rod[1], 121, 110 + tip_dy / 2, 18, 2);
    place(rod[2], 136, 102 + tip_dy, 22, 2);
    int ly = 103 + tip_dy;
    place(line, 157, ly, 1, bob_y - ly > 4 ? bob_y - ly : 4);
    place(bob_top, 155, bob_y, 5, 2);
    place(bob_bot, 155, bob_y + 2, 5, 2);

    for (int i = 0; i < 3; i++) {
      set_vis(splash[i], show_sp);
      if (show_sp) {
        int sp = (int) (f % 4) * 2;
        place(splash[i], 156 + (i - 1) * (5 + sp), 129 - (i == 1 ? 4 : 1) - (f % 2) * 2, 3, 3);
      }
    }

    bool show_fish = (m == PM_DONE || m == PM_SENT_OK) && t < 24;
    set_vis(fish_body, show_fish);
    set_vis(fish_tail, show_fish);
    if (show_fish) {
      int k = (int) (t % 16);
      int arc = k < 8 ? k : (15 - k);
      int fy = 130 - arc * 4;
      place(fish_body, 174, fy, 10, 6);
      place(fish_tail, 183, fy + 1, 4, 4);
    }
    return po;
  }
};

struct TerminalMiningScene {
  lv_obj_t *rail = nullptr, *ties[10] = {};
  lv_obj_t *lantern_glow = nullptr, *lantern_core = nullptr;
  lv_obj_t *rock = nullptr, *ore[3] = {}, *sparks[4] = {}, *gem = nullptr;
  lv_obj_t *helmet = nullptr, *headlamp = nullptr;
  lv_obj_t *pick_h = nullptr, *pick_head = nullptr;
  bool is_codex = false;

  void build(lv_obj_t *card, lv_obj_t *pet_box, lv_obj_t *bubble, bool codex) {
    is_codex = codex;
    rail = blk(card, 0x30363D); place(rail, 0, 137, 540, 2);
    for (int i = 0; i < 10; i++) {
      ties[i] = blk(card, 0x4A311D);
      place(ties[i], 16 + i * 48, 138, 10, 2);
    }
    lantern_glow = blk(card, 0xFFB020, LV_OPA_20, LV_RADIUS_CIRCLE);
    place(lantern_glow, 10, 108, 22, 22);
    lantern_core = blk(card, 0xFFB020, LV_OPA_COVER, 1);
    place(lantern_core, 19, 116, 4, 6);

    rock = blk(card, 0x21262D); place(rock, 478, 96, 62, 42);
    ore[0] = blk(card, codex ? 0x10B981 : 0xFFB020, LV_OPA_COVER, 1); place(ore[0], 486, 104, 10, 5);
    ore[1] = blk(card, codex ? 0xA7F3D0 : 0xD97757, LV_OPA_COVER, 1); place(ore[1], 504, 116, 12, 6);
    ore[2] = blk(card, 0x58A6FF, LV_OPA_COVER, 1); place(ore[2], 490, 126, 8, 5);

    lv_obj_move_foreground(pet_box);
    lv_obj_move_foreground(bubble);

    helmet = blk(card, 0xFFB020, LV_OPA_COVER, 2);
    headlamp = blk(card, 0xFFF3C4, LV_OPA_COVER, 1);
    pick_h = blk(card, 0x8B6F4E);
    pick_head = blk(card, 0xC9D1D9);
    for (int i = 0; i < 4; i++) sparks[i] = blk(card, i % 2 ? 0xFFB020 : 0xF0F6FC);
    gem = blk(card, 0x58A6FF, LV_OPA_COVER, 2);
  }

  PoseOffset tick(PetMode m, uint32_t f, uint32_t t, int &pet_x) {
    PoseOffset po;
    const bool obstructed = (m == PM_PENDING || m == PM_RATE_LIMITED);
    set_vis(lantern_glow, !obstructed);
    set_vis(lantern_core, !obstructed);
    set_vis(rock, !obstructed);
    for (auto *o : ore) set_vis(o, !obstructed);
    for (auto *ti : ties) set_vis(ti, !obstructed);

    if (obstructed) {
      pet_x = 242;
      set_vis(helmet, false); set_vis(headlamp, false);
      set_vis(pick_h, false); set_vis(pick_head, false);
      for (auto *sp : sparks) set_vis(sp, false);
      set_vis(gem, false);
      return po;
    }

    set_vis(helmet, true); set_vis(headlamp, true);
    set_vis(pick_h, true); set_vis(pick_head, true);

    bool impact = false;
    int swing = 0;
    if (m == PM_WORKING) {
      pet_x = 414;
      int k = (int) (f % 8);
      if (k < 3) { swing = 1; po.arm_r = -10; po.override_arms = true; }
      else if (k <= 5) { swing = 2; po.dx = 3; po.arm_r = 2; po.override_arms = true; impact = (k == 3 || k == 4); }
      po.eye_dx = 2; po.override_eye_dx = true;
    } else if (m == PM_DONE || m == PM_SENT_OK) {
      pet_x = 414;
      swing = 1;
    } else {
      pet_x = (m == PM_IDLE) ? 28 : 242;
    }

    int hy = 104 + po.dy;
    if (m == PM_DONE && t < 9) {
      static const int HOP[] = {0, -1, -2, -3, -3, -2, -1, 0, 0};
      hy += HOP[t] * 4;
    }
    place(helmet, pet_x + 16 + po.dx, hy, 22, 5);
    place(headlamp, pet_x + 34 + po.dx, hy + 1, 4, 3);

    int px = pet_x + 48 + po.dx;
    if (swing == 1) {
      place(pick_h, px, 102 + po.dy, 2, 16);
      place(pick_head, px - 6, 99 + po.dy, 12, 3);
    } else if (swing == 2) {
      place(pick_h, px, 118, 16, 2);
      place(pick_head, px + 14, 112, 3, 14);
    } else {
      place(pick_h, px, 118, 12, 2);
      place(pick_head, px + 10, 115, 3, 10);
    }

    lv_obj_set_style_bg_color(ore[1], lv_color_hex(impact ? 0xFFF3C4 : (is_codex ? 0xA7F3D0 : 0xD97757)), 0);
    for (int i = 0; i < 4; i++) {
      set_vis(sparks[i], impact);
      if (impact) {
        int age = (int) (f % 2);
        place(sparks[i], 474 - (i + 1) * 4 - age * 4, 116 + (i - 1) * 4 + age * 2, 3, 3);
      }
    }
    bool show_gem = (m == PM_DONE || m == PM_SENT_OK) && t < 24;
    set_vis(gem, show_gem);
    if (show_gem) place(gem, 492, 92 - (t < 8 ? (int) t * 2 : 16), 10, 10);
    return po;
  }
};

struct TerminalPingPongScene {
  lv_obj_t *table_top = nullptr, *table_line = nullptr, *net = nullptr, *legs[2] = {};
  lv_obj_t *pad_h = nullptr, *pad_f = nullptr;
  lv_obj_t *rival_b = nullptr, *rival_f = nullptr, *ball = nullptr, *bshad = nullptr;
  bool is_codex = false;

  void build(lv_obj_t *card, lv_obj_t *pet_box, lv_obj_t *bubble, bool codex) {
    is_codex = codex;
    legs[0] = blk(card, 0x30363D); place(legs[0], 204, 125, 4, 12);
    legs[1] = blk(card, 0x30363D); place(legs[1], 334, 125, 4, 12);
    table_top = blk(card, 0x1F6FEB); place(table_top, 192, 121, 156, 5);
    table_line = blk(card, 0xF0F6FC); place(table_line, 192, 121, 156, 2);
    net = blk(card, 0xC9D1D9); place(net, 269, 112, 2, 9);

    lv_obj_move_foreground(pet_box);
    lv_obj_move_foreground(bubble);

    pad_h = blk(card, 0x8B6F4E);
    pad_f = blk(card, codex ? 0x10B981 : 0xF85149, LV_OPA_COVER, 2);
    rival_b = blk(card, 0x21262D, LV_OPA_COVER, 2);
    rival_f = blk(card, codex ? 0xD97757 : 0x10B981, LV_OPA_COVER, 1);
    bshad = blk(card, 0x000000, LV_OPA_40, LV_RADIUS_CIRCLE);
    ball = blk(card, 0xFFFFFF, LV_OPA_COVER, LV_RADIUS_CIRCLE);
  }

  PoseOffset tick(PetMode m, uint32_t f, uint32_t t, int &pet_x) {
    PoseOffset po;
    const bool obstructed = (m == PM_PENDING || m == PM_RATE_LIMITED);
    set_vis(table_top, !obstructed); set_vis(table_line, !obstructed);
    set_vis(net, !obstructed); set_vis(legs[0], !obstructed); set_vis(legs[1], !obstructed);
    set_vis(pad_h, !obstructed); set_vis(pad_f, !obstructed);
    set_vis(rival_b, !obstructed); set_vis(rival_f, !obstructed);
    set_vis(ball, !obstructed);

    if (obstructed) {
      pet_x = 242;
      set_vis(bshad, false);
      return po;
    }

    pet_x = 132;
    int bx = 268, by = 116, ry = 108, rx = 356, py = 114;
    if (m == PM_WORKING) {
      static const int BX[16] = {192, 212, 234, 256, 280, 304, 326, 348, 344, 322, 298, 274, 250, 226, 204, 190};
      static const int BY[16] = {108, 102, 99, 101, 107, 117, 110, 106, 105, 100, 98, 101, 108, 117, 111, 109};
      int k = (int) (f % 16);
      bx = BX[k]; by = BY[k];
      ry = by - 5;
      if (k >= 6 && k <= 9) rx = 352;
      if (k == 15 || k <= 1) { po.dx = 3; po.arm_r = -6; po.override_arms = true; py = 107; }
      po.eye_dx = (bx > 270) ? 2 : 0;
      po.override_eye_dx = true;
    } else if (m == PM_NEEDS_INPUT) {
      static const int JUG[6] = {0, -6, -10, -11, -7, -2};
      bx = 186; by = 106 + JUG[f % 6];
    } else if (m == PM_DONE || m == PM_SENT_OK) {
      bx = (t < 10) ? 196 + (int) t * 17 : 368;
      by = (t < 10) ? 108 - (int) (t < 5 ? t * 2 : (10 - t)) : 133;
      py = 98;
    }

    place(pad_h, pet_x + 52 + po.dx, py + 8, 3, 6);
    place(pad_f, pet_x + 50 + po.dx, py, 7, 10);
    place(rival_b, rx, ry, 9, 16);
    place(rival_f, rx - 2, ry + 2, 3, 12);
    bool on_tbl = (bx >= 192 && bx <= 346 && by <= 122);
    set_vis(bshad, on_tbl);
    if (on_tbl) place(bshad, bx, 120, 5, 2);
    place(ball, bx, by, 5, 5);
    return po;
  }
};

struct TerminalCampfireScene {
  lv_obj_t *stars[4] = {};
  lv_obj_t *glow = nullptr, *logs[2] = {};
  lv_obj_t *f_out = nullptr, *f_mid = nullptr, *f_core = nullptr;
  lv_obj_t *embers[3] = {}, *stick = nullptr, *mallow = nullptr;

  void build(lv_obj_t *card, lv_obj_t *pet_box, lv_obj_t *bubble, bool /*codex*/) {
    static const int SX[4] = {44, 152, 396, 488}, SY[4] = {98, 104, 96, 102};
    for (int i = 0; i < 4; i++) {
      stars[i] = blk(card, 0x8B949E);
      place(stars[i], SX[i], SY[i], 2, 2);
    }
    glow = blk(card, 0x4A2612, LV_OPA_70, LV_RADIUS_CIRCLE); place(glow, 314, 132, 52, 8);
    logs[0] = blk(card, 0x5A3E28); place(logs[0], 326, 133, 28, 4);
    logs[1] = blk(card, 0x3D291A); place(logs[1], 330, 131, 20, 4);

    lv_obj_move_foreground(pet_box);
    lv_obj_move_foreground(bubble);

    stick = blk(card, 0x8B6F4E);
    mallow = blk(card, 0xF0F6FC, LV_OPA_COVER, 1);
    f_out = blk(card, 0xF85149, LV_OPA_COVER, 4);
    f_mid = blk(card, 0xFFB020, LV_OPA_COVER, 3);
    f_core = blk(card, 0xFFE082, LV_OPA_COVER, 2);
    for (int i = 0; i < 3; i++) embers[i] = blk(card, i == 1 ? 0xF85149 : 0xFFB020);
  }

  PoseOffset tick(PetMode m, uint32_t f, uint32_t t, int &pet_x) {
    PoseOffset po;
    const bool obstructed = (m == PM_PENDING || m == PM_RATE_LIMITED);
    for (auto *s : stars) set_vis(s, !obstructed);
    set_vis(glow, !obstructed); set_vis(logs[0], !obstructed); set_vis(logs[1], !obstructed);
    set_vis(f_out, !obstructed); set_vis(f_mid, !obstructed); set_vis(f_core, !obstructed);
    set_vis(stick, !obstructed); set_vis(mallow, !obstructed);

    if (obstructed) {
      pet_x = 242;
      for (auto *e : embers) set_vis(e, false);
      return po;
    }

    pet_x = 236;
    for (int i = 0; i < 4; i++) {
      bool br = ((f / 6 + i * 2) % 5) == 0;
      lv_obj_set_style_bg_color(stars[i], lv_color_hex(br ? 0xF0F6FC : 0x6E7681), 0);
    }

    bool low = (m == PM_IDLE);
    int fl = (int) (f % 4);
    int oh = low ? 6 + (fl % 2) : 18 + (fl == 1 ? 4 : fl == 3 ? -2 : 0);
    int mh = low ? 4 : 12 + ((fl + 1) % 3) * 2;
    int ch = low ? 2 : 6 + (fl % 2) * 2;
    place(f_out, 331 + (low ? 0 : fl % 2), 133 - oh, 16, oh);
    place(f_mid, 334, 133 - mh, 10, mh);
    place(f_core, 336, 133 - ch, 6, ch);

    for (int i = 0; i < 3; i++) {
      set_vis(embers[i], !low);
      if (!low) {
        int age = (int) ((f + i * 6) % 14);
        place(embers[i], 335 + i * 3 + ((age / 3) % 2 ? 2 : -2), 116 - age * 2, 2, 2);
      }
    }

    int sy = (m == PM_WORKING) ? 119 + ((f / 4) % 2 ? -2 : 1) : (m == PM_DONE && t < 8) ? 110 : 122;
    if (m == PM_WORKING) { po.arm_r = -4; po.override_arms = true; po.eye_dx = 2; po.override_eye_dx = true; }
    uint32_t mc = (m == PM_DONE || m == PM_SENT_OK) ? 0xE39B4B : (m == PM_SENT_NO) ? 0x30363D : 0xF0F6FC;
    lv_obj_set_style_bg_color(mallow, lv_color_hex(mc), 0);
    place(stick, pet_x + 52 + po.dx, sy + po.dy, 28, 2);
    place(mallow, pet_x + 78 + po.dx, sy - 2 + po.dy, 5, 5);
    return po;
  }
};

// ── Random Variation Multiplexers ────────────────────────────────────────────

// Shared PRNG and reroll policy for cycling through default + 4 themed scenes.
struct SceneSelector {
  static constexpr int kVariantCount = 5;
  int current = -1;
  uint32_t rng = 0;
  uint32_t last_work_start = UINT32_MAX;
  uint32_t idle_ticks = 0;
  bool was_hidden = true;

  void mix(uint32_t now_ms, uint32_t frame) {
    uint32_t s = rng ^ now_ms ^ (frame * 2654435761U) ^
                 static_cast<uint32_t>(std::time(nullptr)) ^
                 static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
    if (s == 0) s = 0x9E3779B9U;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    rng = s;
  }

  int pick_next() {
    if (current < 0) return static_cast<int>(rng % kVariantCount);
    return (current + 1 + static_cast<int>(rng % (kVariantCount - 1))) % kVariantCount;
  }

  // Returns >= 0 when a new variant should be activated, or -1 if unchanged.
  int poll(PetMode m, uint32_t work_start, uint32_t now_ms, uint32_t frame, bool hidden) {
    if (current < 0) {
      mix(now_ms, frame);
      current = pick_next();
      last_work_start = work_start;
      was_hidden = hidden;
      idle_ticks = 0;
      return current;
    }
    bool reroll = false;
    if (work_start != last_work_start && work_start != 0) {
      last_work_start = work_start;
      reroll = true;
    } else if (was_hidden && !hidden && m != PM_PENDING && m != PM_RATE_LIMITED) {
      reroll = true;
    } else if (m == PM_IDLE && !hidden) {
      if (++idle_ticks >= 600) {
        idle_ticks = 0;
        reroll = true;
      }
    } else {
      idle_ticks = 0;
    }
    was_hidden = hidden;
    if (!reroll) return -1;
    mix(now_ms, frame);
    current = pick_next();
    return current;
  }
};

struct StageSceneMux {
  lv_obj_t *stage = nullptr, *pet_box = nullptr, *bubble = nullptr, *layer = nullptr;
  bool is_codex = false;
  SceneSelector sel;
  FishingScene fishing;
  MiningScene mining;
  PingPongScene pingpong;
  CampfireScene campfire;

  void init(lv_obj_t *s, lv_obj_t *pb, lv_obj_t *bub, bool codex) {
    stage = s;
    pet_box = pb;
    bubble = bub;
    is_codex = codex;
    lv_obj_set_style_clip_corner(stage, true, 0);
  }

  int desired_u() const { return sel.current <= 0 ? 7 : 5; }
  bool has_scene() const { return sel.current > 0; }

  void select(int idx) {
    if (layer != nullptr) {
      lv_obj_set_parent(pet_box, stage);
      lv_obj_set_parent(bubble, stage);
      lv_obj_delete(layer);
      layer = nullptr;
    }
    if (idx == 0) {
      lv_obj_set_pos(pet_box, 26, 55);
      lv_obj_set_pos(bubble, 104, 12);
      lv_obj_move_foreground(pet_box);
      lv_obj_move_foreground(bubble);
      return;
    }
    layer = blk(stage, 0x0D1117, LV_OPA_COVER, 10);
    place(layer, 0, 0, 150, 140);
    lv_obj_set_parent(pet_box, layer);
    lv_obj_set_parent(bubble, layer);
    switch (idx) {
      case 1: fishing = FishingScene{}; fishing.build(layer, pet_box, bubble, is_codex); break;
      case 2: mining = MiningScene{}; mining.build(layer, pet_box, bubble, is_codex); break;
      case 3: pingpong = PingPongScene{}; pingpong.build(layer, pet_box, bubble, is_codex); break;
      case 4: campfire = CampfireScene{}; campfire.build(layer, pet_box, bubble, is_codex); break;
    }
  }

  void maybe_reroll(PetMode m, uint32_t work_start, uint32_t now_ms, uint32_t frame, bool hidden) {
    int next = sel.poll(m, work_start, now_ms, frame, hidden);
    if (next >= 0) select(next);
  }

  PoseOffset tick(PetMode m, uint32_t f, uint32_t t) {
    switch (sel.current) {
      case 1: return fishing.tick(m, f, t);
      case 2: return mining.tick(m, f, t);
      case 3: return pingpong.tick(m, f, t);
      case 4: return campfire.tick(m, f, t);
      default: return PoseOffset{};
    }
  }
};

struct TerminalSceneMux {
  lv_obj_t *card = nullptr, *pet_box = nullptr, *bubble = nullptr, *layer = nullptr;
  bool is_codex = false;
  SceneSelector sel;
  TerminalFishingScene fishing;
  TerminalMiningScene mining;
  TerminalPingPongScene pingpong;
  TerminalCampfireScene campfire;

  void init(lv_obj_t *c, lv_obj_t *pb, lv_obj_t *bub, bool codex) {
    card = c;
    pet_box = pb;
    bubble = bub;
    is_codex = codex;
  }

  bool has_scene() const { return sel.current > 0; }

  void select(int idx) {
    if (layer != nullptr) {
      lv_obj_set_parent(pet_box, card);
      lv_obj_set_parent(bubble, card);
      lv_obj_delete(layer);
      layer = nullptr;
    }
    if (idx == 0) {
      lv_obj_move_foreground(pet_box);
      lv_obj_move_foreground(bubble);
      return;
    }
    layer = blk(card, 0x0D1117, LV_OPA_TRANSP);
    place(layer, 0, 0, 540, 140);
    lv_obj_add_flag(layer, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_move_background(layer);
    lv_obj_set_parent(pet_box, layer);
    lv_obj_set_parent(bubble, layer);
    switch (idx) {
      case 1: fishing = TerminalFishingScene{}; fishing.build(layer, pet_box, bubble, is_codex); break;
      case 2: mining = TerminalMiningScene{}; mining.build(layer, pet_box, bubble, is_codex); break;
      case 3: pingpong = TerminalPingPongScene{}; pingpong.build(layer, pet_box, bubble, is_codex); break;
      case 4: campfire = TerminalCampfireScene{}; campfire.build(layer, pet_box, bubble, is_codex); break;
    }
  }

  void maybe_reroll(PetMode m, uint32_t work_start, uint32_t now_ms, uint32_t frame, bool hidden) {
    int next = sel.poll(m, work_start, now_ms, frame, hidden);
    if (next >= 0) select(next);
  }

  PoseOffset tick(PetMode m, uint32_t f, uint32_t t, int &pet_x) {
    switch (sel.current) {
      case 1: return fishing.tick(m, f, t, pet_x);
      case 2: return mining.tick(m, f, t, pet_x);
      case 3: return pingpong.tick(m, f, t, pet_x);
      case 4: return campfire.tick(m, f, t, pet_x);
      default: return PoseOffset{};
    }
  }
};

}  // namespace pet_scenes
