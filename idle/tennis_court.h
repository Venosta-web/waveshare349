#pragma once
// "Tennis" Lock Screen Variant: two mascots rally across a side-view court
// under the clock. The Matchup follows the AI Feature Modules in the build:
// Claude only (or neither) is Clawd vs Clawd, Codex only is Byte vs Byte, and
// both is Clawd (left) vs Byte (right). Mirror matches tell the players apart
// with headbands; the mixed match needs none. Built once into the court container from idle/tennis_card.yaml
// and stepped at 20 Hz by idle/tennis.yaml while the Lock Screen is shown.
//
// A point: the server waits ~1.5 s, tosses and serves; shots alternate, each
// bouncing once on the far side before the receiver returns it. After a random
// 6-12 hits the receiver lunges and misses, the last hitter celebrates, and
// the winner serves the next point.
#include <lvgl.h>
#include <cstdlib>
#include "clawd.h"
#include "codex_pet.h"

namespace tennis {

// Court geometry, in court-container px (the container spans the full 640 px
// width and is anchored so y = 0 is the overlay top).
constexpr int W = 640;
constexpr int GROUND = 150;  // court surface: Clawd's feet and the ball bounce here
constexpr int NET_X = 320;
constexpr int NET_TOP = 124;
constexpr int U = 5;             // Clawd unit size, px
constexpr int PW = 14 * U;       // puppet width
constexpr int ROOT_Y = GROUND - 9 * U;
constexpr int HIT_Y = ROOT_Y;    // racket head height when striking
constexpr int HEADROOM = 5 * U;  // extra sprite-box space above the puppet for hops
constexpr int BALL = 6;
constexpr int FPS = 20;

enum Kind : uint8_t { CLAWD, BYTE };

struct Player {
  Kind kind = CLAWD;
  clawd::Puppet cp;      // used when kind == CLAWD
  codex_pet::Puppet bp;  // used when kind == BYTE
  bool banded = false;
  lv_obj_t *band = nullptr, *tail = nullptr, *handle = nullptr, *head = nullptr;
  bool left = true;  // left player faces right (towards the net)
  int x = 0;         // puppet root x
  int target_x = 0;
  int swing = 0;     // frames of follow-through left
  int dir() const { return left ? 1 : -1; }
  // Where the racket strikes the ball for a given root x.
  int hit_x() const { return left ? x + PW + 10 : x - 10; }
  static int root_for_hit(bool left_side, int hx) { return left_side ? hx - PW - 10 : hx + 10; }
};

enum Phase : uint8_t { SERVE_WAIT, TOSS, FLIGHT, POINT };

struct Shot {
  int x0, y0, bx, x1, y1;  // strike -> bounce (bx, GROUND) -> next strike
  int h1, h2;              // arc heights of the two segments, px
  int f1, f2;              // frames per segment
  bool miss;               // receiver whiffs; ball runs off the court
};

struct State {
  lv_obj_t *court = nullptr;
  lv_obj_t *ball = nullptr, *ball_shadow = nullptr;
  Player pl[2];
  Phase phase = SERVE_WAIT;
  int server = 0;    // 0 = left, 1 = right
  int hitter = 0;    // who struck the ball in flight
  int hits = 0, hits_target = 8;
  int winner = 0;
  uint32_t t = 0;    // frames in phase / shot
  uint32_t f = 0;    // frames since build
  Shot shot{};
  int bx = 0, by = 0;
};

inline State g;

inline int rnd(int lo, int hi) { return lo + (int) (lv_rand(0, (uint32_t) (hi - lo))); }

inline lv_obj_t *blk(lv_obj_t *parent, uint32_t color, int x, int y, int w, int h,
                     lv_opa_t opa = LV_OPA_COVER, int radius = 0) {
  lv_obj_t *o = clawd::block(parent, color, opa);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  return o;
}

// band = 0 means no headband.
inline void build_player(Player &pl, bool left, Kind kind, uint32_t band, uint32_t rim) {
  pl.left = left;
  pl.kind = kind;
  pl.banded = band != 0;
  lv_obj_t *root = lv_obj_create(g.court);
  lv_obj_remove_style_all(root);
  if (kind == CLAWD) clawd::build(pl.cp, root, U);
  else codex_pet::build(pl.bp, root, U);
  // The sprite box clips anything drawn above it (a hop, Byte's antenna), so
  // grow it upwards; top padding keeps the puppet's own coordinates unchanged.
  lv_obj_set_style_pad_top(root, HEADROOM, 0);
  // (Use the puppet's own height: LVGL has not laid the box out yet.)
  lv_obj_set_height(root, (kind == CLAWD ? pl.cp.h() : pl.bp.h()) + HEADROOM);
  // Accessories live on the court (not the puppet root, which clips the
  // racket once it is raised above the mascot) and follow it each frame.
  pl.tail = clawd::block(g.court, band);
  pl.band = clawd::block(g.court, band);
  if (!pl.banded) {
    lv_obj_add_flag(pl.tail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pl.band, LV_OBJ_FLAG_HIDDEN);
  }
  pl.handle = clawd::block(g.court, 0x8B6F4E);
  pl.head = clawd::block(g.court, 0xF8FAFC, LV_OPA_20);
  lv_obj_set_style_radius(pl.head, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(pl.head, 2, 0);
  lv_obj_set_style_border_color(pl.head, lv_color_hex(rim), 0);
  pl.x = pl.target_x = left ? 70 : W - 70 - PW;
}

inline void build(lv_obj_t *court, bool has_claude, bool has_codex) {
  g = State{};
  g.court = court;
  lv_obj_add_flag(court, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  // Court: hard-court surface with white baselines, apron below.
  blk(court, 0x0B1A2A, 0, GROUND, W, 172 - GROUND);
  blk(court, 0x1D4ED8, 24, GROUND, W - 48, 7);
  blk(court, 0xF8FAFC, 24, GROUND, W - 48, 1);
  blk(court, 0xF8FAFC, 24, GROUND, 3, 7);
  blk(court, 0xF8FAFC, W - 27, GROUND, 3, 7);
  blk(court, 0x3B82F6, 24, GROUND + 7, W - 48, 1, LV_OPA_40);

  // Net, edge-on: mesh, centre post and white tape.
  blk(court, 0xCBD5E1, NET_X - 3, NET_TOP, 6, GROUND - NET_TOP, LV_OPA_30);
  blk(court, 0x64748B, NET_X - 1, NET_TOP, 2, GROUND - NET_TOP);
  blk(court, 0xF8FAFC, NET_X - 4, NET_TOP - 2, 8, 3, LV_OPA_COVER, 1);

  if (has_claude && has_codex) {  // Clawd vs Byte, racket rims in brand colours
    build_player(g.pl[0], true, CLAWD, 0, clawd::BODY);
    build_player(g.pl[1], false, BYTE, 0, codex_pet::BODY);
  } else if (has_codex) {         // Byte vs Byte: white and amber headbands
    build_player(g.pl[0], true, BYTE, 0xF8FAFC, 0xF85149);
    build_player(g.pl[1], false, BYTE, 0xF59E0B, 0x38BDF8);
  } else {                        // Clawd vs Clawd: white and teal headbands
    build_player(g.pl[0], true, CLAWD, 0xF8FAFC, 0xF85149);
    build_player(g.pl[1], false, CLAWD, 0x2DD4BF, 0x38BDF8);
  }

  g.ball_shadow = blk(court, 0x000000, 0, GROUND - 1, 8, 3, LV_OPA_40, LV_RADIUS_CIRCLE);
  g.ball = blk(court, 0xD9F99D, 0, 0, BALL, BALL, LV_OPA_COVER, LV_RADIUS_CIRCLE);

  g.server = (int) lv_rand(0, 1);
  g.phase = SERVE_WAIT;
}

// Parabolic hop between two points; s in [0, 1], h = height above the chord.
inline int arc_y(int y0, int y1, int h, int k, int n) {
  if (n <= 0) return y1;
  const int lin = y0 + (y1 - y0) * k / n;
  return lin - 4 * h * k * (n - k) / (n * n);
}

// Plan the next shot from `from` to the other player.
inline void plan_shot(int from, bool serve) {
  Player &a = g.pl[from];
  Player &b = g.pl[1 - from];
  const int d = a.dir();
  Shot s{};
  s.x0 = a.hit_x();
  s.y0 = serve ? HIT_Y - 10 : HIT_Y;
  // Bounce somewhere in the receiver's half, then the receiver strikes it further on.
  s.bx = NET_X + d * rnd(70, 140);
  s.miss = (g.hits + 1) >= g.hits_target;
  if (s.miss) {
    s.x1 = d > 0 ? W + 20 : -20;  // sails past the receiver and off the court
    s.y1 = HIT_Y - 20;
    s.f2 = 14;
    // Receiver lunges, but stops short of the ball's path.
    int reach = s.bx + d * rnd(40, 70);
    b.target_x = Player::root_for_hit(!a.left, reach - d * 30);
  } else {
    s.x1 = s.bx + d * rnd(60, 100);
    s.y1 = HIT_Y;
    s.f2 = 9;
    b.target_x = Player::root_for_hit(!a.left, s.x1);
  }
  s.h1 = rnd(34, 44);
  s.h2 = rnd(10, 18);
  s.f1 = rnd(16, 20);
  // Keep both Clawds on their own side of the court.
  if (b.left) b.target_x = LV_CLAMP(4, b.target_x, NET_X - PW - 40);
  else b.target_x = LV_CLAMP(NET_X + 40, b.target_x, W - PW - 4);
  g.shot = s;
  g.hitter = from;
  g.hits++;
  a.swing = 5;
  g.t = 0;
}

// Pose one Clawd and place its headband and racket for this frame.
inline void pose_player(int i) {
  Player &pl = g.pl[i];
  clawd::Pose s;
  const int px = 1;
  const bool moving = pl.x != pl.target_x;
  const int to_ball = g.bx > pl.x + PW / 2 ? 1 : -1;
  s.eye_dx = to_ball * (U / 2);
  if ((g.f % 53) < 2) s.eye_h = px;
  int racket_up = 0;  // extra lift of the racket arm
  int racket_fwd = 0; // racket head pushed towards the net

  if (g.phase == POINT) {
    if (i == g.winner) {  // hop with racket held high
      static const int HOP[] = {0, -1, -2, -3, -3, -2, -1, 0, 0, 0, -1, -2, -2, -1, 0, 0};
      int k = (int) g.t % 16;
      s.dy = HOP[k] * U;
      s.arm_l = s.arm_r = -3 * U;  // both arms up; the racket rides the net-side one
      if (k == 7) s.squash = U / 2;
    } else {  // shake head, then droop
      static const int SHAKE[] = {0, 2, -2, 2, -2, 1, -1, 0};
      if (g.t < 8) s.dx = SHAKE[g.t];
      s.eye_h = U / 2;
      s.arm_l = s.arm_r = U / 2;
      racket_up = U / 2;
      s.eye_dx = 0;
    }
  } else if (moving) {  // trot
    int ph = g.f % 4;
    if (ph == 0) { s.lift[0] = s.lift[2] = U; s.dy = -px; }
    if (ph == 2) { s.lift[1] = s.lift[3] = U; s.dy = -px; }
  } else if ((g.f / 8) % 2) {
    s.squash = px;  // ready-stance breathing
  }

  if (pl.swing > 0) {  // follow-through
    racket_up -= U;
    racket_fwd = pl.swing > 2 ? 2 * U : U;
    pl.swing--;
  }
  if (g.phase == TOSS && i == g.server) racket_up -= 2 * U;  // racket back for the serve

  // The racket arm is the one nearest the net.
  if (pl.left) s.arm_r += racket_up; else s.arm_l += racket_up;
  if (pl.kind == CLAWD) {
    clawd::apply(pl.cp, s);
    lv_obj_set_pos(pl.cp.root, pl.x, ROOT_Y - HEADROOM);
  } else {
    // Byte has two boots: map Clawd's leg pairs onto them.
    codex_pet::Pose b;
    b.dx = s.dx; b.dy = s.dy; b.squash = s.squash;
    b.lift[0] = s.lift[0]; b.lift[1] = s.lift[1];
    b.arm_l = s.arm_l; b.arm_r = s.arm_r;
    b.eye_dx = s.eye_dx; b.eye_h = s.eye_h;
    const bool lost = g.phase == POINT && i != g.winner;
    codex_pet::apply(pl.bp, b, lost ? codex_pet::SENT_NO : codex_pet::DONE, g.f);
    lv_obj_set_pos(pl.bp.root, pl.x, ROOT_Y - HEADROOM);
  }

  // Headband across the brow, knot tail flapping away from the net.
  const int ox = pl.x, oy = ROOT_Y;  // puppet root origin on the court
  const int top = oy + 2 * U + s.dy + s.squash;
  // Byte's chassis has rounded corners, so its band sits a unit further in.
  const int band_in = pl.kind == BYTE ? U : 0;
  lv_obj_set_pos(pl.band, ox + s.dx + 2 * U + band_in, top);
  lv_obj_set_size(pl.band, 10 * U - 2 * band_in, U - 1);
  const int flap = moving ? ((g.f / 2) % 2) : 0;
  lv_obj_set_size(pl.tail, U + 2, 3);
  lv_obj_set_pos(pl.tail, ox + (pl.left ? s.dx + 2 * U + band_in - (U + 2) : s.dx + 12 * U - band_in), top + 1 + flap);

  // Racket: handle rising from the net-side arm, oval head on top.
  const int arm_x = ox + (pl.left ? s.dx + 12 * U : s.dx);
  const int arm_y = top + 2 * U + (pl.left ? s.arm_r : s.arm_l);
  const int hx = arm_x + U - 1 + pl.dir() * racket_fwd;
  lv_obj_set_pos(pl.handle, arm_x + U - 1 + pl.dir() * (racket_fwd / 2), arm_y - 2 * U);
  lv_obj_set_size(pl.handle, 3, 3 * U);
  lv_obj_set_size(pl.head, 3 * U + 2, 4 * U);
  lv_obj_set_pos(pl.head, hx - (3 * U + 2) / 2 + 1, arm_y - 6 * U);
}

inline void place_ball(int x, int y) {
  g.bx = x;
  g.by = y;
  lv_obj_set_pos(g.ball, x - BALL / 2, y - BALL / 2);
  int air = GROUND - y;
  if (air < 0) air = 0;
  int sw = 10 - air / 8;
  if (sw < 4) sw = 4;
  lv_obj_set_size(g.ball_shadow, sw, 3);
  lv_obj_set_pos(g.ball_shadow, x - sw / 2, GROUND - 1);
  lv_obj_set_style_bg_opa(g.ball_shadow, air > 30 ? LV_OPA_20 : LV_OPA_40, 0);
  const bool on = x > -BALL && x < W + BALL;
  if (on) {
    lv_obj_remove_flag(g.ball, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g.ball_shadow, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g.ball, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g.ball_shadow, LV_OBJ_FLAG_HIDDEN);
  }
}

// Advance the rally by one frame (call at FPS while the Lock Screen is shown).
inline void tick() {
  if (g.court == nullptr) return;
  g.f++;
  g.t++;

  switch (g.phase) {
    case SERVE_WAIT: {
      // Server walks back to the baseline; receiver to the middle of their half.
      Player &sv = g.pl[g.server];
      Player &rc = g.pl[1 - g.server];
      sv.target_x = sv.left ? 40 : W - 40 - PW;
      rc.target_x = rc.left ? 110 : W - 110 - PW;
      place_ball(sv.hit_x(), HIT_Y - 4);
      if (g.t >= (uint32_t) (FPS * 3 / 2) && sv.x == sv.target_x) {
        g.phase = TOSS;
        g.t = 0;
      }
      break;
    }
    case TOSS: {  // ball goes up and comes down to the racket
      Player &sv = g.pl[g.server];
      const int n = 12;
      place_ball(sv.hit_x(), arc_y(HIT_Y - 4, HIT_Y - 10, 36, (int) g.t, n));
      if ((int) g.t >= n) {
        g.hits = 0;
        g.hits_target = rnd(6, 12);
        plan_shot(g.server, true);
        g.phase = FLIGHT;
      }
      break;
    }
    case FLIGHT: {
      const Shot &s = g.shot;
      const int k = (int) g.t;
      if (k <= s.f1) {
        place_ball(s.x0 + (s.bx - s.x0) * k / s.f1, arc_y(s.y0, GROUND - BALL / 2, s.h1, k, s.f1));
      } else {
        const int k2 = k - s.f1;
        place_ball(s.bx + (s.x1 - s.bx) * k2 / s.f2, arc_y(GROUND - BALL / 2, s.y1, s.h2, k2, s.f2));
        if (k2 >= s.f2) {
          if (s.miss) {
            g.winner = g.hitter;
            g.phase = POINT;
            g.t = 0;
          } else {
            plan_shot(1 - g.hitter, false);
          }
        }
      }
      break;
    }
    case POINT: {
      g.pl[0].target_x = g.pl[0].x;
      g.pl[1].target_x = g.pl[1].x;
      place_ball(-100, GROUND);
      if (g.t >= (uint32_t) (FPS * 2)) {
        g.server = g.winner;
        g.phase = SERVE_WAIT;
        g.t = 0;
      }
      break;
    }
  }

  // Run towards targets: 4 px/frame, a bit faster when the ball is coming.
  for (auto &pl : g.pl) {
    const int step = g.phase == FLIGHT ? 6 : 4;
    const int d = pl.target_x - pl.x;
    if (d > step) pl.x += step;
    else if (d < -step) pl.x -= step;
    else pl.x = pl.target_x;
  }
  pose_player(0);
  pose_player(1);
}

}  // namespace tennis
