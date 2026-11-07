#pragma once
#include "player_config.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace player {
struct Report {
    bool connected = true, sensors_valid = true;
    uint16_t buttons = 0;
    uint8_t lx = 128, ly = 128, rx = 128, ry = 128, l2 = 0, r2 = 0;
    std::array<uint16_t, 4> sensors{512, 399, 512, 512};
};
struct MotionPulse {
    uint64_t start = 0;
    bool held = false, active = false;
};
inline uint8_t stick(float negative, float positive) {
    return (uint8_t)std::clamp(std::lround(128.f - 128.f * negative + 127.f * positive), 0l, 255l);
}
inline uint16_t sensor(float value) { return (uint16_t)std::clamp(std::lround(value), 0l, 1023l); }
inline bool finite_motion(const std::array<float, 3> &a, const std::array<float, 3> &g) {
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(a[i]) || !std::isfinite(g[i]))
            return false;
    return true;
}
inline Report make_report(const std::array<float, ACTION_COUNT> &values, MotionMode mode, bool real_motion,
                          const std::array<float, 3> &accel, const std::array<float, 3> &gyro, uint64_t now,
                          MotionPulse &pulse) {
    Report r;
    for (int a = 0; a < 16; ++a)
        if (values[a] > 0.25f)
            r.buttons |= (uint16_t)(1u << a);
    r.l2 = (uint8_t)std::clamp(std::lround(values[L2] * 255), 0l, 255l);
    r.r2 = (uint8_t)std::clamp(std::lround(values[R2] * 255), 0l, 255l);
    r.lx = stick(values[L_LEFT], values[L_RIGHT]);
    r.ly = stick(values[L_UP], values[L_DOWN]);
    r.rx = stick(values[R_LEFT], values[R_RIGHT]);
    r.ry = stick(values[R_UP], values[R_DOWN]);
    if (mode != MotionMode::RightStick && real_motion && finite_motion(accel, gyro)) {
        for (int i = 0; i < 3; ++i)
            r.sensors[i] = sensor(512.f - accel[i] * (113.f / 9.80665f));
        r.sensors[3] = sensor(512.f + gyro[1] * (180.f / 3.14159265358979323846f) * (123.f / 90.f));
    } else {
        float x = values[R_RIGHT] - values[R_LEFT], y = values[R_DOWN] - values[R_UP];
        r.sensors[0] = sensor(512.f + x * (x < 0 ? 512.f : 511.f));
        r.sensors[1] = sensor(399.f + y * (y < 0 ? 399.f : 624.f));
        r.sensors[3] = sensor(512.f + x * 123.f);
    }
    bool press = values[MOTION_THROW] > 0.25f;
    if (press && !pulse.held) {
        pulse.start = now;
        pulse.active = true;
    }
    pulse.held = press;
    if (pulse.active) {
        uint64_t elapsed = now - pulse.start;
        if (elapsed < 100)
            r.sensors[2] = 1023;
        else if (elapsed < 200)
            r.sensors[2] = 0;
        else
            pulse.active = false;
    }
    return r;
}
} // namespace player
