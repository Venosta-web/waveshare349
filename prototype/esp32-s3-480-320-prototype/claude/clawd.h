#pragma once
// Clawd, the Claude Code mascot, drawn as an LVGL pixel puppet, plus the state
// shared by both AI card variants (claude/stage.yaml, claude/terminal.yaml).
//
// The sprite is a 14 x 9 unit grid (one unit = `u` px):
//
//   ..BBBBBBBBBB..   B body      E eye (dark slot in the body)
//   ..BBEBBBBEBB..   A arm       L leg
//   AABBEBBBBEBBAA
//   AABBBBBBBBBBAA
//   ..BBBBBBBBBB..
//   ...L.L..L.L...
//   ...L.L..L.L...
//
// Two units of headroom sit above the body so hops and raised arms stay inside
// the root, and a soft shadow sits under the feet.
#include <lvgl.h>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace clawd {

enum Mode : uint8_t { IDLE, WORKING, PENDING, NEEDS_INPUT, DONE, SENT_OK, SENT_NO, RATE_LIMITED };

constexpr uint32_t BODY = 0xD97757;  // Claude orange
constexpr uint32_t EYE = 0x0A0C10;
constexpr uint32_t SHADOW = 0x000000;

// Accent per mode, used for the status word, spinner and border.
inline uint32_t accent(Mode m) {
  switch (m) {
    case WORKING: return 0xD97757;
    case PENDING: return 0xFFB020;
    case NEEDS_INPUT: return 0xB197FC;
    case DONE: return 0x3FB950;
    case SENT_OK: return 0x3FB950;
    case SENT_NO: return 0xF85149;
    case RATE_LIMITED: return 0xF85149;
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
inline uint64_t g_rl_total_ms = 5ULL * 3600ULL * 1000ULL;
inline uint64_t g_rl_remaining_ms = 0;
inline uint32_t g_rl_set_ms = 0;
inline bool g_rl_is_7d = false;
inline std::string g_rl_reset_at;
inline int g_rl_7d_left_pct = -1;
inline std::string g_rl_7d_reset_at;
inline std::string g_project, g_text;
inline int g_step_cur = 0;
inline int g_step_total = 0;
inline std::string g_step_now, g_step_next;

inline Mode parse_status(const std::string &s) {
  if (s == "Working") return WORKING;
  if (s == "Pending Approval") return PENDING;
  if (s == "Needs Input") return NEEDS_INPUT;
  if (s == "Done" || s == "Ready") return DONE;  // "Ready" from older bridges
  if (s == "Rate Limited") return RATE_LIMITED;
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

// Parse "<window>|<remaining_sec>|<reset_str>|[7d=<left_pct>;<7d_reset>|]<rest>" or fall back to plain text.
inline void parse_rate_limit_detail(const std::string &d, uint32_t now_ms) {
  g_rl_set_ms = now_ms;
  g_rl_7d_left_pct = -1;
  g_rl_7d_reset_at.clear();
  if (d.rfind("5h|", 0) == 0 || d.rfind("7d|", 0) == 0) {
    g_rl_is_7d = (d[0] == '7');
    g_rl_total_ms = (g_rl_is_7d ? 7ULL * 86400ULL : 5ULL * 3600ULL) * 1000ULL;
    size_t p1 = 3;
    size_t p2 = d.find('|', p1);
    if (p2 != std::string::npos) {
      uint64_t rem_s = std::strtoull(d.substr(p1, p2 - p1).c_str(), nullptr, 10);
      g_rl_remaining_ms = rem_s * 1000ULL;
      if (g_rl_remaining_ms > g_rl_total_ms) g_rl_total_ms = g_rl_remaining_ms;
      size_t p3 = d.find('|', p2 + 1);
      if (p3 != std::string::npos) {
        g_rl_reset_at = d.substr(p2 + 1, p3 - p2 - 1);
        size_t rest_pos = p3 + 1;
        if (d.compare(rest_pos, 3, "7d=") == 0) {
          size_t p4 = d.find('|', rest_pos);
          if (p4 != std::string::npos) {
            std::string sd = d.substr(rest_pos + 3, p4 - (rest_pos + 3));
            size_t semi = sd.find(';');
            int pct = atoi((semi == std::string::npos ? sd : sd.substr(0, semi)).c_str());
            if (pct >= 0 && pct <= 100) {
              g_rl_7d_left_pct = pct;
              if (semi != std::string::npos) g_rl_7d_reset_at = sd.substr(semi + 1);
            }
            rest_pos = p4 + 1;
          }
        }
        split_detail(d.substr(rest_pos));
        return;
      }
    }
  }
  g_rl_is_7d = d.find("7d") != std::string::npos || d.find("7-day") != std::string::npos ||
               d.find("weekly") != std::string::npos || d.find("Weekly") != std::string::npos;
  g_rl_total_ms = (g_rl_is_7d ? 7ULL * 86400ULL : 5ULL * 3600ULL) * 1000ULL;
  g_rl_remaining_ms = g_rl_total_ms;
  g_rl_reset_at.clear();
  split_detail(d);
}

inline uint64_t rl_left_ms(uint32_t now_ms) {
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

// Claude Code's own spinner, ping-ponged.
inline const char *spinner(uint32_t f) {
  static const char *const SEQ[] = {"·", "✢", "✳", "✶", "✻", "✽", "✻", "✶", "✳", "✢"};
  return SEQ[f % 10];
}

// ── Puppet ─────────────────────────────────────────────────────────────────
struct Pose {
  int dx = 0, dy = 0;            // whole sprite offset, px (dy < 0 is up)
  int squash = 0;                // px taken off the body height (landing, breathing)
  int lift[4] = {0, 0, 0, 0};    // px each leg is drawn up off the floor
  int arm_l = 0, arm_r = 0;      // arm vertical offset, px (negative is up)
  int eye_dx = 0;                // gaze, px
  int eye_h = -1;                // eye height px; -1 = fully open
};

struct Puppet {
  lv_obj_t *root = nullptr;
  lv_obj_t *body, *arm_l, *arm_r, *eye[2], *leg[4], *shadow;
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
  p.shadow = block(root, SHADOW, LV_OPA_40);
  lv_obj_set_style_radius(p.shadow, LV_RADIUS_CIRCLE, 0);
  for (auto &l : p.leg) l = block(root, BODY);
  p.arm_l = block(root, BODY);
  p.arm_r = block(root, BODY);
  p.body = block(root, BODY);
  for (auto &e : p.eye) e = block(root, EYE);
}

inline void apply(Puppet &p, const Pose &s) {
  const int u = p.u;
  const int bh = 5 * u - s.squash;
  const int top = 2 * u + s.dy + s.squash;  // squash keeps the feet planted
  const int x0 = s.dx;

  lv_obj_set_pos(p.body, x0 + 2 * u, top);
  lv_obj_set_size(p.body, 10 * u, bh);

  lv_obj_set_size(p.arm_l, 2 * u, 2 * u);
  lv_obj_set_size(p.arm_r, 2 * u, 2 * u);
  lv_obj_set_pos(p.arm_l, x0, top + 2 * u + s.arm_l);
  lv_obj_set_pos(p.arm_r, x0 + 12 * u, top + 2 * u + s.arm_r);

  const int eh = s.eye_h < 0 ? 2 * u : s.eye_h;
  const int ey = top + u + (2 * u - eh) / 2;
  lv_obj_set_size(p.eye[0], u, eh);
  lv_obj_set_size(p.eye[1], u, eh);
  lv_obj_set_pos(p.eye[0], x0 + 4 * u + s.eye_dx, ey);
  lv_obj_set_pos(p.eye[1], x0 + 9 * u + s.eye_dx, ey);

  static const int LEG_X[4] = {3, 5, 8, 10};
  for (int i = 0; i < 4; i++) {
    int lh = 2 * u - s.lift[i];
    if (lh < 1) lh = 1;
    lv_obj_set_pos(p.leg[i], x0 + LEG_X[i] * u, top + bh);
    lv_obj_set_size(p.leg[i], u, lh);
  }

  // The shadow stays on the floor and shrinks as Clawd leaves it.
  int air = -s.dy;
  if (air < 0) air = 0;
  int sw = 11 * u - air * 2;
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
    case IDLE: {  // asleep: eyes shut, slow breath
      s.eye_h = px;
      s.squash = (f / 12) % 2 ? px : 0;
      break;
    }
    case WORKING: {  // trot in place: alternate leg pairs, body bobs on each step
      int ph = f % 4;
      if (ph == 0) { s.lift[0] = s.lift[2] = u; s.dy = -px; }
      if (ph == 2) { s.lift[1] = s.lift[3] = u; s.dy = -px; }
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
    case NEEDS_INPUT: {  // look around, scratch head
      int g = (f / 14) % 4;
      s.eye_dx = g == 0 ? -half : g == 2 ? half : 0;
      s.arm_r = (f / 4) % 2 ? -3 * u : -3 * u + px;
      if (blink) s.eye_h = px;
      break;
    }
    case DONE:
    case SENT_OK: {  // hop with arms up on entry, then awake and blinking
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
    case SENT_NO: {  // head shake
      static const int SHAKE[] = {0, 2, -2, 2, -2, 1, -1, 0};
      if (t < 8) s.dx = SHAKE[t] * px;
      s.eye_h = u;
      break;
    }
    case RATE_LIMITED: {  // tired cooldown: half-lidded eyes, drooped arms, slow sigh
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

// Update the sidebar nav Clawd icon so it always mirrors Clawd's current state.
inline void tick_nav(lv_obj_t *box) {
  static Puppet np;
  if (np.root == nullptr) build(np, box, 2);
  apply(np, animate(g_mode, g_frame, g_frame - g_mode_frame, np.u));
}

}  // namespace clawd
