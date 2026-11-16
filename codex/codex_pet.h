#pragma once
// Byte, the Codex mascot, drawn as an LVGL pixel puppet, plus the state
// shared by both Codex card variants (codex/stage.yaml, codex/terminal.yaml).
//
// The sprite is a 14 x 9 unit grid (one unit = `u` px), matching Clawd's
// footprint so it fits the same nav rail (u = 2), terminal floor (u = 4),
// and stage (u = 7) containers:
//
//   ......TT......   T antenna tip     S antenna stalk
//   ......SS......   B chassis body    V dark visor
//   ..BBBBBBBBBB..   E glowing eye     A articulated arm
//   ..BVEEBBEEVB..   L sturdy boot
//   AABVVVVVVVVBAA
//   AABBBBBBBBBBAA
//   ..BBBBBBBBBB..
//   ....LL..LL....
//   ....LL..LL....
//
// Two units of headroom sit above the body for the antenna and hops;
// OVERFLOW_VISIBLE keeps jumps and raised arms clean inside the root.
#include <lvgl.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

namespace codex_pet {

enum Mode : uint8_t { IDLE, WORKING, PENDING, NEEDS_INPUT, DONE, SENT_OK, SENT_NO, RATE_LIMITED, TURN_FAILED };

constexpr uint32_t BODY = 0x10B981;       // Codex emerald
constexpr uint32_t BODY_DARK = 0x059669;  // Deep emerald for arms & boots
constexpr uint32_t VISOR = 0x0A0C10;      // Obsidian visor screen
constexpr uint32_t EYE_GLOW = 0xA7F3D0;   // Mint-white glowing eyes
constexpr uint32_t SHADOW = 0x000000;

// Accent per mode, used for the status word, spinner, and border.
inline uint32_t accent(Mode m) {
  switch (m) {
    case WORKING: return 0x10B981;
    case PENDING: return 0xFFB020;
    case NEEDS_INPUT: return 0xB197FC;
    case DONE: return 0x3FB950;
    case SENT_OK: return 0x3FB950;
    case SENT_NO: return 0xF85149;
    case RATE_LIMITED: return 0xF85149;
    case TURN_FAILED: return 0xF85149;
    default: return 0x8B949E;
  }
}

// ── Shared runtime state ───────────────────────────────────────────────────
inline Mode g_mode = IDLE;
inline uint32_t g_frame = 0;        // 10 Hz ticks since boot
inline uint32_t g_mode_frame = 0;   // tick when g_mode last changed
inline uint32_t g_work_start = 0;   // millis() when the current prompt started
inline uint32_t g_pending_start = 0;
inline uint32_t g_last_run = 0;      // ms the last finished prompt took (0 = unknown)
inline uint64_t g_rl_total_ms = 1;
inline uint64_t g_rl_remaining_ms = 0;
inline uint64_t g_rl_reset_epoch = 0;
inline uint32_t g_rl_set_ms = 0;
inline bool g_rl_has_reset = false;
inline std::string g_rl_detail;
inline std::string g_rl_reset_at;
inline std::string g_project, g_text;
inline int g_step_cur = 0;
inline int g_step_total = 0;
inline std::string g_step_now, g_step_next;

inline Mode parse_status(const std::string &s) {
  if (s == "Working") return WORKING;
  if (s == "Pending Approval") return PENDING;
  if (s == "Needs Input") return NEEDS_INPUT;
  if (s == "Done" || s == "Ready") return DONE;
  if (s == "Rate Limited") return RATE_LIMITED;
  if (s == "Turn Failed") return TURN_FAILED;
  return IDLE;
}

// Bridge detail is "project | text"; anything without a separator is all text.
inline void split_detail(const std::string &d) {
  auto bar = d.find(" | ");
  if (bar == std::string::npos) {
    g_project.clear();
    g_text = d;
  } else {
    g_project = d.substr(0, bar);
    g_text = d.substr(bar + 3);
  }
}

// Parse "<cur>/<total>|<current>|<next>" into g_step_*; clears on empty/invalid.
inline void parse_steps(const std::string &s) {
  g_step_cur = 0;
  g_step_total = 0;
  g_step_now.clear();
  g_step_next.clear();
  if (s.empty() || s == "unknown" || s == "unavailable") return;
  auto p1 = s.find('|');
  auto p2 = p1 == std::string::npos ? std::string::npos : s.find('|', p1 + 1);
  if (p1 == std::string::npos || p2 == std::string::npos) return;
  std::string counts = s.substr(0, p1);
  auto slash = counts.find('/');
  if (slash == std::string::npos) return;
  int cur = atoi(counts.substr(0, slash).c_str());
  int total = atoi(counts.substr(slash + 1).c_str());
  std::string now = s.substr(p1 + 1, p2 - p1 - 1);
  if (cur <= 0 || total <= 0 || now.empty()) return;
  g_step_cur = cur > total ? total : cur;
  g_step_total = total;
  g_step_now = now;
  g_step_next = s.substr(p2 + 1);
}

// Switch mode, keeping the work timer running across approve/needs-input
// round trips inside the same prompt.
inline void set_mode(Mode m, uint32_t now_ms) {
  if (m == g_mode) return;
  if (m == RATE_LIMITED) g_rl_detail.clear();
  if (m == WORKING && g_mode != PENDING && g_mode != NEEDS_INPUT && g_mode != SENT_OK && g_mode != SENT_NO)
    g_work_start = now_ms;
  if (m == PENDING) g_pending_start = now_ms;
  if (m == DONE) g_last_run = g_mode == IDLE ? 0 : now_ms - g_work_start;
  g_mode = m;
  g_mode_frame = g_frame;
}

inline void fmt_elapsed(char *buf, size_t n, uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s < 60) snprintf(buf, n, "%us", (unsigned) s);
  else if (s < 3600) snprintf(buf, n, "%um %02us", (unsigned) (s / 60), (unsigned) (s % 60));
  else snprintf(buf, n, "%uh %02um", (unsigned) (s / 3600), (unsigned) ((s / 60) % 60));
}

inline const char *working_label() {
  return g_step_total > 0 && !g_text.empty() && g_text != "Turn in progress" ? g_text.c_str() : "Working";
}

// Reset fields are present only when the bridge has a confirmed cooldown.
inline void parse_rate_limit_detail(const std::string &d, uint32_t now_ms) {
  if (d == g_rl_detail && !d.empty()) return;
  g_rl_detail = d;
  g_rl_set_ms = now_ms;
  g_rl_has_reset = false;
  g_rl_total_ms = 1;
  g_rl_remaining_ms = 0;
  g_rl_reset_epoch = 0;
  g_rl_reset_at.clear();
  const bool absolute = d.rfind("reset|", 0) == 0;
  const bool legacy = d.rfind("5h|", 0) == 0 || d.rfind("7d|", 0) == 0;
  if (absolute || legacy) {
    size_t p1 = absolute ? d.find('|', 6) : 2;
    size_t p2 = p1 == std::string::npos ? p1 : d.find('|', p1 + 1);
    if (p2 != std::string::npos) {
      size_t p3 = d.find('|', p2 + 1);
      const auto remaining = d.substr(p1 + 1, p2 - p1 - 1);
      const auto epoch = absolute ? d.substr(6, p1 - 6) : "0";
      if (p3 != std::string::npos && !remaining.empty() && remaining.size() <= 12 &&
          remaining.find_first_not_of("0123456789") == std::string::npos && !epoch.empty() &&
          epoch.size() <= 12 && epoch.find_first_not_of("0123456789") == std::string::npos) {
        g_rl_remaining_ms = std::strtoull(remaining.c_str(), nullptr, 10) * 1000ULL;
        g_rl_reset_epoch = std::strtoull(epoch.c_str(), nullptr, 10);
        g_rl_total_ms = legacy ? (d[0] == '7' ? 604800000ULL : 18000000ULL) : 1;
        if (g_rl_remaining_ms > g_rl_total_ms) g_rl_total_ms = g_rl_remaining_ms;
        g_rl_has_reset = true;
        g_rl_reset_at = d.substr(p2 + 1, p3 - p2 - 1);
        split_detail(d.substr(p3 + 1));
        return;
      }
    }
  }
  split_detail(d);
}

inline uint64_t rl_left_ms(uint32_t now_ms) {
  if (g_rl_reset_epoch) {
    const auto epoch = std::time(nullptr);
    if (epoch >= 1577836800) {
      const auto now = static_cast<uint64_t>(epoch);
      return now < g_rl_reset_epoch ? (g_rl_reset_epoch - now) * 1000ULL : 0;
    }
  }
  uint32_t elapsed = now_ms - g_rl_set_ms;
  return elapsed < g_rl_remaining_ms ? g_rl_remaining_ms - elapsed : 0;
}

inline void fmt_countdown(char *buf, size_t n, uint64_t ms) {
  if (ms == 0) {
    snprintf(buf, n, "0s");
    return;
  }
  uint64_t s = (ms + 999) / 1000;
  if (s >= 86400) {
    snprintf(buf, n, "%ud %02uh %02um", (unsigned) (s / 86400), (unsigned) ((s % 86400) / 3600),
             (unsigned) ((s % 3600) / 60));
  } else if (s >= 3600) {
    snprintf(buf, n, "%uh %02um %02us", (unsigned) (s / 3600), (unsigned) ((s % 3600) / 60),
             (unsigned) (s % 60));
  } else if (s >= 60) {
    snprintf(buf, n, "%um %02us", (unsigned) (s / 60), (unsigned) (s % 60));
  } else {
    snprintf(buf, n, "%us", (unsigned) s);
  }
}

// Ping-ponged CLI spinner matching the Claude tab.
inline const char *spinner(uint32_t f) {
  static const char *const SEQ[] = {"·", "✢", "✳", "✶", "✻", "✽", "✻", "✶", "✳", "✢"};
  return SEQ[f % 10];
}

// ── Puppet ─────────────────────────────────────────────────────────────────
struct Pose {
  int dx = 0, dy = 0;            // whole sprite offset, px (dy < 0 is up)
  int squash = 0;                // px taken off the body height (landing, breathing)
  int lift[2] = {0, 0};          // px each boot is drawn up off the floor
  int arm_l = 0, arm_r = 0;      // arm vertical offset, px (negative is up)
  int eye_dx = 0;                // gaze, px
  int eye_h = -1;                // eye height px; -1 = fully open
};

struct Puppet {
  lv_obj_t *root = nullptr;
  lv_obj_t *body, *visor, *chest_led, *antenna_stalk, *antenna_tip;
  lv_obj_t *arm_l, *arm_r, *eye[2], *leg[2], *shadow;
  int u = 6;
  int w() const { return 14 * u; }
  int h() const { return 9 * u + u / 2 + 3; }
};

inline lv_obj_t *block(lv_obj_t *parent, uint32_t color, lv_opa_t opa = LV_OPA_COVER) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_remove_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  return o;
}

// Build the sprite inside `root` (an empty, unstyled container from YAML).
inline void build(Puppet &p, lv_obj_t *root, int u) {
  p.root = root;
  p.u = u;
  lv_obj_set_size(root, p.w(), p.h());
  lv_obj_add_flag(root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_remove_flag(root, (lv_obj_flag_t) (LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

  const int r_body = u;
  const int r_part = u / 2 > 0 ? u / 2 : 1;
  const int r_eye = u / 3 > 0 ? u / 3 : 1;

  p.shadow = block(root, SHADOW, LV_OPA_40);
  lv_obj_set_style_radius(p.shadow, LV_RADIUS_CIRCLE, 0);

  for (auto &l : p.leg) {
    l = block(root, BODY_DARK);
    lv_obj_set_style_radius(l, r_eye, 0);
  }
  p.arm_l = block(root, BODY_DARK);
  p.arm_r = block(root, BODY_DARK);
  lv_obj_set_style_radius(p.arm_l, r_part, 0);
  lv_obj_set_style_radius(p.arm_r, r_part, 0);

  p.antenna_stalk = block(root, BODY_DARK);
  p.antenna_tip = block(root, EYE_GLOW);
  lv_obj_set_style_radius(p.antenna_tip, r_part, 0);

  p.body = block(root, BODY);
  lv_obj_set_style_radius(p.body, r_body, 0);

  p.visor = block(root, VISOR);
  lv_obj_set_style_radius(p.visor, r_part, 0);

  p.chest_led = block(root, BODY_DARK);
  lv_obj_set_style_radius(p.chest_led, r_eye, 0);

  for (auto &e : p.eye) {
    e = block(root, EYE_GLOW);
    lv_obj_set_style_radius(e, r_eye, 0);
  }
}

inline void apply(Puppet &p, const Pose &s, Mode m, uint32_t f) {
  const int u = p.u;
  const int bh = 5 * u - s.squash;
  const int top = 2 * u + s.dy + s.squash;  // squash keeps the feet planted
  const int x0 = s.dx;
  const int px = u / 4 > 0 ? u / 4 : 1;

  // Antenna sits centered atop the chassis.
  lv_obj_set_pos(p.antenna_stalk, x0 + 6 * u + u / 2, top - u);
  lv_obj_set_size(p.antenna_stalk, u, u + 1);
  lv_obj_set_pos(p.antenna_tip, x0 + 6 * u, top - 2 * u);
  lv_obj_set_size(p.antenna_tip, 2 * u, u);

  uint32_t tip_col = EYE_GLOW;
  if (m == IDLE) tip_col = 0x374151;
  else if (m == PENDING) tip_col = (f / 3) % 2 ? 0xFFB020 : 0xFFE082;
  else if (m == NEEDS_INPUT) tip_col = 0xB197FC;
  else if (m == RATE_LIMITED || m == SENT_NO) tip_col = 0xF85149;
  else if (m == WORKING) tip_col = (f / 4) % 2 ? 0xA7F3D0 : 0x34D399;
  lv_obj_set_style_bg_color(p.antenna_tip, lv_color_hex(tip_col), 0);

  // Chassis body
  lv_obj_set_pos(p.body, x0 + 2 * u, top);
  lv_obj_set_size(p.body, 10 * u, bh);

  // Dark visor inset
  const int vh = 2 * u + u / 2;
  const int vy = top + u - (s.squash > 0 ? px : 0);
  lv_obj_set_pos(p.visor, x0 + 3 * u, vy);
  lv_obj_set_size(p.visor, 8 * u, vh);

  // Chest LED bar
  lv_obj_set_pos(p.chest_led, x0 + 5 * u, top + bh - u);
  lv_obj_set_size(p.chest_led, 4 * u, px);
  uint32_t led_col = m == IDLE ? 0x065F46 : m == PENDING ? 0xFFB020 : m == RATE_LIMITED ? 0xF85149 : EYE_GLOW;
  lv_obj_set_style_bg_color(p.chest_led, lv_color_hex(led_col), 0);

  // Arms
  lv_obj_set_size(p.arm_l, 2 * u, 2 * u);
  lv_obj_set_size(p.arm_r, 2 * u, 2 * u);
  lv_obj_set_pos(p.arm_l, x0, top + 2 * u + s.arm_l);
  lv_obj_set_pos(p.arm_r, x0 + 12 * u, top + 2 * u + s.arm_r);

  // Glowing eyes inside visor
  const int eh_full = u + u / 2;
  const int eh = s.eye_h < 0 ? eh_full : s.eye_h;
  const int ew = u + u / 2 > 0 ? u + u / 2 : 1;
  const int ey = vy + (vh - eh) / 2;
  lv_obj_set_size(p.eye[0], ew, eh);
  lv_obj_set_size(p.eye[1], ew, eh);
  lv_obj_set_pos(p.eye[0], x0 + 4 * u + s.eye_dx, ey);
  lv_obj_set_pos(p.eye[1], x0 + 8 * u + u / 2 + s.eye_dx, ey);

  uint32_t eye_col = EYE_GLOW;
  if (m == IDLE) eye_col = 0x6EE7B7;
  else if (m == PENDING) eye_col = 0xFFB020;
  else if (m == SENT_NO || m == RATE_LIMITED) eye_col = 0xFF7B72;
  lv_obj_set_style_bg_color(p.eye[0], lv_color_hex(eye_col), 0);
  lv_obj_set_style_bg_color(p.eye[1], lv_color_hex(eye_col), 0);

  // Two sturdy boots
  static const int LEG_X[2] = {4, 8};
  for (int i = 0; i < 2; i++) {
    int lh = 2 * u - s.lift[i];
    if (lh < 1) lh = 1;
    lv_obj_set_pos(p.leg[i], x0 + LEG_X[i] * u, top + bh);
    lv_obj_set_size(p.leg[i], 2 * u, lh);
  }

  // The shadow stays on the floor and shrinks as Byte leaves it.
  int air = -s.dy;
  if (air < 0) air = 0;
  int sw = 10 * u - air * 2;
  if (sw < 4 * u) sw = 4 * u;
  const int sh = u / 2 + 3;
  lv_obj_set_size(p.shadow, sw, sh);
  lv_obj_set_pos(p.shadow, x0 + 7 * u - sw / 2, 9 * u - sh / 2 + 1);
  lv_obj_set_style_bg_opa(p.shadow, air > 0 ? LV_OPA_20 : LV_OPA_40, 0);
}

// One frame of the character animation for a mode. `t` = ticks in this mode.
inline Pose animate(Mode m, uint32_t f, uint32_t t, int u) {
  Pose s;
  const int half = u / 2 > 0 ? u / 2 : 1;
  const int px = u / 4 > 0 ? u / 4 : 1;
  const bool blink = (f % 47) < 2;
  switch (m) {
    case IDLE: {  // sleep mode: visor eyes dimmed to thin lines, slow breath
      s.eye_h = px;
      s.squash = (f / 12) % 2 ? px : 0;
      break;
    }
    case WORKING: {  // brisk march in place: alternate boots, arms swing, body bobs
      int ph = f % 4;
      if (ph == 0) { s.lift[0] = u; s.dy = -px; }
      if (ph == 2) { s.lift[1] = u; s.dy = -px; }
      s.arm_l = ph == 0 ? -half : ph == 2 ? half : 0;
      s.arm_r = -s.arm_l;
      if (blink) s.eye_h = px;
      break;
    }
    case PENDING: {  // wave for attention, a little hop every 1.2 s
      bool left = (f / 3) % 2;
      s.arm_l = left ? -3 * u : 0;
      s.arm_r = left ? 0 : -3 * u;
      int k = f % 12;
      if (k == 0 || k == 2) s.dy = -half;
      if (k == 1) s.dy = -u;
      if (k == 3) s.squash = px;
      break;
    }
    case NEEDS_INPUT: {  // scan left/right, scratch antenna
      int g = (f / 14) % 4;
      s.eye_dx = g == 0 ? -half : g == 2 ? half : 0;
      s.arm_r = (f / 4) % 2 ? -3 * u : -3 * u + px;
      if (blink) s.eye_h = px;
      break;
    }
    case DONE:
    case SENT_OK: {  // celebratory hop with arms up on entry, then awake and blinking
      static const int HOP[] = {0, -1, -2, -3, -3, -2, -1, 0, 0};
      if (t < 9) {
        s.dy = HOP[t] * u;
        if (HOP[t] < 0) s.arm_l = s.arm_r = -3 * u;
        if (t == 7) s.squash = half;
      } else {
        if (blink) s.eye_h = px;
        if ((f / 30) % 4 == 3) s.eye_dx = half;
      }
      break;
    }
    case SENT_NO:
    case TURN_FAILED: {  // head shake
      static const int SHAKE[] = {0, 2, -2, 2, -2, 1, -1, 0};
      if (t < 8) s.dx = SHAKE[t] * px;
      s.eye_h = half;
      break;
    }
    case RATE_LIMITED: {  // low-power cooldown: half-lidded eyes, drooped arms, slow sigh
      s.eye_h = half > 0 ? half : 1;
      s.arm_l = half;
      s.arm_r = half;
      s.squash = (f / 16) % 2 ? px : 0;
      if ((f % 60) < 3) s.eye_h = px;
      break;
    }
  }
  return s;
}

// Update the sidebar nav Codex icon so it always mirrors Byte's current state.
inline void tick_nav(lv_obj_t *box) {
  static Puppet np;
  if (np.root == nullptr) build(np, box, 2);
  apply(np, animate(g_mode, g_frame, g_frame - g_mode_frame, np.u), g_mode, g_frame);
}

}  // namespace codex_pet
