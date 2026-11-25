#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace labyrinth {

struct Point {
  float x;
  float y;
};

struct Wall {
  int x;
  int y;
  int width;
  int height;
};

struct Hole {
  Point center;
  bool goal;
};

inline constexpr int board_width = 410;
inline constexpr int board_height = 142;
inline constexpr int marble_radius = 6;
inline constexpr int hole_radius = 10;
inline constexpr Point start{25, 31};

inline constexpr Wall walls[]{
    {78, 0, 10, 98}, {156, 44, 10, 98}, {234, 0, 10, 98}, {312, 44, 10, 98},
    {88, 66, 24, 8}, {166, 87, 24, 8}, {244, 65, 24, 8}, {322, 78, 32, 8},
};

inline constexpr Hole holes[]{
    {{59, 109}, false}, {{135, 32}, false}, {{215, 110}, false}, {{292, 33}, false},
    {{383, 110}, true},
};

enum class Phase { Ready, Rolling, Won, Lost };

class Game {
 public:
  Phase phase() const { return phase_; }
  Point position() const { return position_; }

  void reset() {
    phase_ = Phase::Ready;
    position_ = start;
    velocity_ = {};
  }

  // Acceleration is already in screen coordinates, with positive values
  // pointing downhill. Keep a filtered sample ready before the Start tap.
  void sample(Point tilt, uint32_t now_ms) {
    if (!std::isfinite(tilt.x) || !std::isfinite(tilt.y))
      return;

    if (has_motion(now_ms)) {
      filtered_.x += 0.28f * (tilt.x - filtered_.x);
      filtered_.y += 0.28f * (tilt.y - filtered_.y);
    } else {
      filtered_ = tilt;
    }
    sample_ms_ = now_ms;
    sampled_ = true;
  }

  bool has_motion(uint32_t now_ms) const {
    return sampled_ && now_ms - sample_ms_ <= 400;
  }

  bool begin(uint32_t now_ms) {
    if (!has_motion(now_ms))
      return false;

    reset();
    neutral_ = filtered_;
    frame_ms_ = now_ms;
    phase_ = Phase::Rolling;
    return true;
  }

  void advance(uint32_t now_ms) {
    if (phase_ != Phase::Rolling || !has_motion(now_ms))
      return;

    // Short substeps keep wall contact stable, including after a delayed frame.
    const uint32_t elapsed = std::min(now_ms - frame_ms_, uint32_t{64});
    frame_ms_ = now_ms;
    if (elapsed == 0)
      return;

    const unsigned steps = (elapsed + 7) / 8;
    const float dt = elapsed * 0.001f / steps;
    const Point acceleration{steering(filtered_.x - neutral_.x),
                             steering(filtered_.y - neutral_.y)};
    for (unsigned i = 0; i < steps && phase_ == Phase::Rolling; ++i)
      step(acceleration, dt);
  }

 private:
  static float steering(float tilt) {
    constexpr float dead_zone = 0.025f;
    const float magnitude = std::clamp(std::abs(tilt) - dead_zone, 0.0f, 0.40f);
    return std::copysign(720.0f * magnitude, tilt);
  }

  static bool blocked(Point point) {
    if (point.x < marble_radius || point.x > board_width - marble_radius ||
        point.y < marble_radius || point.y > board_height - marble_radius)
      return true;

    for (const Wall &wall : walls) {
      const float dx = point.x - std::clamp(point.x, float(wall.x), float(wall.x + wall.width));
      const float dy = point.y - std::clamp(point.y, float(wall.y), float(wall.y + wall.height));
      if (dx * dx + dy * dy < marble_radius * marble_radius)
        return true;
    }
    return false;
  }

  void step(Point acceleration, float dt) {
    constexpr float speed_limit = 145.0f;
    const float drag = 1.0f - 2.4f * dt;
    velocity_.x = (velocity_.x + acceleration.x * dt) * drag;
    velocity_.y = (velocity_.y + acceleration.y * dt) * drag;
    const float speed = std::hypot(velocity_.x, velocity_.y);
    if (speed > speed_limit) {
      velocity_.x *= speed_limit / speed;
      velocity_.y *= speed_limit / speed;
    }

    const Point before = position_;
    const Point across{position_.x + velocity_.x * dt, position_.y};
    if (blocked(across))
      velocity_.x = 0;
    else
      position_ = across;

    const Point down{position_.x, position_.y + velocity_.y * dt};
    if (blocked(down))
      velocity_.y = 0;
    else
      position_ = down;

    // Check the marble center's path so even a brief crossing counts.
    const Point travel{position_.x - before.x, position_.y - before.y};
    const float distance_squared = travel.x * travel.x + travel.y * travel.y;
    for (const Hole &hole : holes) {
      const Point offset{hole.center.x - before.x, hole.center.y - before.y};
      const float along = distance_squared > 0
                              ? std::clamp((offset.x * travel.x + offset.y * travel.y) /
                                               distance_squared,
                                           0.0f, 1.0f)
                              : 0.0f;
      const float dx = offset.x - along * travel.x;
      const float dy = offset.y - along * travel.y;
      if (dx * dx + dy * dy < hole_radius * hole_radius) {
        position_ = hole.center;
        phase_ = hole.goal ? Phase::Won : Phase::Lost;
        return;
      }
    }
  }

  Phase phase_{Phase::Ready};
  Point position_{start};
  Point velocity_{};
  Point filtered_{};
  Point neutral_{};
  uint32_t sample_ms_{0};
  uint32_t frame_ms_{0};
  bool sampled_{false};
};

inline Game &game() {
  static Game instance;
  return instance;
}

}  // namespace labyrinth
