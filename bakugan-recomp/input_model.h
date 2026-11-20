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
// Apply a 2D radial deadzone (avoiding cardinal snapping near axes) and expand
// circular gamepad travel onto [-1, 1]^2 while preserving exact stick angle.
inline void shape_stick_2d(float raw_x, float raw_y, float deadzone, float &out_x, float &out_y) {
    float r = std::hypot(raw_x, raw_y);
    if (r <= deadzone || r < 1e-6f) {
        out_x = 0.0f;
        out_y = 0.0f;
        return;
    }
    float outer = std::max(0.85f, deadzone + 0.10f);
    float mag = std::clamp((r - deadzone) / (outer - deadzone), 0.0f, 1.0f);
    float ux = raw_x / r;
    float uy = raw_y / r;
    float max_c = std::max(std::abs(ux), std::abs(uy));
    out_x = (ux / max_c) * mag;
    out_y = (uy / max_c) * mag;
}
inline uint16_t sensor(float value) {
    return std::isfinite(value) ? (uint16_t)std::clamp(std::lround(value), 0l, 1023l) : 512;
}
inline bool finite_motion(const std::array<float, 3> &a, const std::array<float, 3> &g) {
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(a[i]) || !std::isfinite(g[i]))
            return false;
    return true;
}
// Apply a circular radial deadzone and smooth cubic-blend response curve on [-1, 1]^2
// while preserving exact input angle and reaching full corners at maximum tilt.
inline void shape_sixaxis_2d(float x, float y, float deadzone, float curve, float &out_x, float &out_y) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        out_x = 0.0f;
        out_y = 0.0f;
        return;
    }
    x = std::clamp(x, -1.0f, 1.0f);
    y = std::clamp(y, -1.0f, 1.0f);
    float r = std::hypot(x, y);
    float dz = std::clamp(deadzone, 0.0f, 0.95f);
    if (r <= dz || r < 1e-6f) {
        out_x = 0.0f;
        out_y = 0.0f;
        return;
    }
    float ux = x / r;
    float uy = y / r;
    float max_c = std::max(std::abs(ux), std::abs(uy));
    float r_edge = 1.0f / max_c;
    float t = (r_edge > dz + 1e-5f) ? std::clamp((r - dz) / (r_edge - dz), 0.0f, 1.0f) : 1.0f;
    float c = std::clamp(curve, 0.0f, 1.0f);
    float curved = t * (1.0f - c) + (t * t * t) * c;
    out_x = (ux / max_c) * curved;
    out_y = (uy / max_c) * curved;
}
inline float ema_step(float current, float target, float dt_sec, float tau_sec, float adaptive_boost = 0.0f) {
    if (!std::isfinite(current))
        current = 0.0f;
    if (!std::isfinite(target))
        target = 0.0f;
    if (!std::isfinite(dt_sec) || dt_sec <= 0.0f)
        return current;
    if (tau_sec <= 1e-4f)
        return target;
    float eff_tau = tau_sec / (1.0f + adaptive_boost * std::abs(target - current));
    float alpha = std::clamp(1.0f - std::exp(-dt_sec / eff_tau), 0.0f, 1.0f);
    float next = current + (target - current) * alpha;
    if (std::abs(target) < 1e-5f && std::abs(next) < 5e-4f)
        return 0.0f;
    return next;
}
struct SixaxisState {
    float tilt_x = 0.0f;
    float tilt_y = 0.0f;
    std::array<float, 3> accel{0.0f, 9.80665f, 0.0f};
    std::array<float, 3> gyro{0.0f, 0.0f, 0.0f};
    bool accel_seeded = false;
};
inline void step_sixaxis(SixaxisState &s, float raw_tilt_x, float raw_tilt_y, float tilt_deadzone,
                         bool has_real, const std::array<float, 3> &raw_accel,
                         const std::array<float, 3> &raw_gyro, const Config &cfg, float dt_sec,
                         float &out_tilt_x, float &out_tilt_y, std::array<float, 3> &out_accel,
                         std::array<float, 3> &out_gyro) {
    float tau = std::clamp(cfg.sixaxis_smoothing, 0.0f, 1.0f) * 0.065f;
    float sens = std::clamp(cfg.gyro_sensitivity, 0.1f, 5.0f);
    float gyro_tilt_x = 0.0f, gyro_tilt_y = 0.0f;
    bool real_active = has_real && cfg.motion != MotionMode::RightStick && finite_motion(raw_accel, raw_gyro);

    if (real_active) {
        if (!s.accel_seeded) {
            s.accel = raw_accel;
            s.accel_seeded = true;
        } else {
            for (int i = 0; i < 3; ++i)
                s.accel[i] = ema_step(s.accel[i], raw_accel[i], dt_sec, tau * 0.6f, 1.5f);
        }
        float raw_pitch = raw_gyro[0], raw_yaw = raw_gyro[1], raw_roll = raw_gyro[2];
        float speed = std::hypot(raw_yaw, raw_pitch);
        float gz_dz = std::max(0.0f, cfg.gyro_deadzone);
        if (speed <= gz_dz || speed < 1e-6f) {
            raw_yaw = 0.0f;
            raw_pitch = 0.0f;
        } else {
            float scale = (speed - gz_dz) / speed;
            raw_yaw *= scale;
            raw_pitch *= scale;
        }
        if (std::abs(raw_roll) <= gz_dz)
            raw_roll = 0.0f;
        else
            raw_roll -= std::copysign(gz_dz, raw_roll);

        float sign_x = cfg.gyro_invert_x ? 1.0f : -1.0f;
        float sign_y = cfg.gyro_invert_y ? -1.0f : 1.0f;
        float steer_rate = raw_yaw + raw_roll;
        s.gyro[0] = ema_step(s.gyro[0], raw_pitch * sign_y * sens, dt_sec, tau * 0.5f, 2.0f);
        s.gyro[1] = ema_step(s.gyro[1], steer_rate, dt_sec, tau * 0.5f, 2.0f);
        s.gyro[2] = ema_step(s.gyro[2], raw_roll * sens, dt_sec, tau * 0.5f, 2.0f);

        gyro_tilt_x = std::clamp((steer_rate * sign_x * sens) / 1.40f, -1.0f, 1.0f);
        gyro_tilt_y = std::clamp((raw_pitch * sign_y * sens) / 1.40f, -1.0f, 1.0f);
    } else {
        s.accel_seeded = false;
        s.accel[0] = ema_step(s.accel[0], 0.0f, dt_sec, tau * 0.6f, 1.5f);
        s.accel[1] = ema_step(s.accel[1], 9.80665f, dt_sec, tau * 0.6f, 1.5f);
        s.accel[2] = ema_step(s.accel[2], 0.0f, dt_sec, tau * 0.6f, 1.5f);
        for (int i = 0; i < 3; ++i)
            s.gyro[i] = ema_step(s.gyro[i], 0.0f, dt_sec, tau * 0.5f, 2.0f);
    }

    float stick_x = (cfg.motion == MotionMode::Sensors && real_active) ? 0.0f : raw_tilt_x;
    float stick_y = (cfg.motion == MotionMode::Sensors && real_active) ? 0.0f : raw_tilt_y;
    float combined_x = std::clamp(stick_x + gyro_tilt_x, -1.0f, 1.0f);
    float combined_y = std::clamp(stick_y + gyro_tilt_y, -1.0f, 1.0f);
    float target_x = 0.0f, target_y = 0.0f;
    shape_sixaxis_2d(combined_x, combined_y, tilt_deadzone, cfg.sixaxis_curve, target_x, target_y);
    s.tilt_x = ema_step(s.tilt_x, target_x, dt_sec, tau, 2.5f);
    s.tilt_y = ema_step(s.tilt_y, target_y, dt_sec, tau, 2.5f);
    out_tilt_x = std::clamp(s.tilt_x, -1.0f, 1.0f);
    out_tilt_y = std::clamp(s.tilt_y, -1.0f, 1.0f);
    out_accel = s.accel;
    out_gyro = s.gyro;
}
// Integrate mouse pixel deltas into a leaky virtual stick so turns hold smoothly
// across mouse sweeps, counter-steering responds immediately, and short ticks cannot spike.
inline void step_mouse_virtual_stick(float &virt_x, float &virt_y, int raw_dx, int raw_dy,
                                     float sensitivity, float dt_sec) {
    if (!std::isfinite(virt_x)) virt_x = 0.0f;
    if (!std::isfinite(virt_y)) virt_y = 0.0f;
    if (!std::isfinite(dt_sec) || dt_sec <= 0.0f) return;

    float sens = std::clamp(sensitivity, 0.05f, 2.0f);
    float pix = std::hypot((float)raw_dx, (float)raw_dy);
    constexpr float kDeadzonePx = 1.2f;
    bool moving = pix > kDeadzonePx;
    float decay = std::exp(-dt_sec / (moving ? 0.45f : 0.22f));
    virt_x *= decay;
    virt_y *= decay;

    if (moving) {
        float active = (pix - kDeadzonePx) / pix;
        float dx = (float)raw_dx * active;
        float dy = (float)raw_dy * active;
        float gain = sens * 0.0085f;
        float gx = (dx * virt_x < 0.0f) ? gain * 2.0f : gain;
        float gy = (dy * virt_y < 0.0f) ? gain * 2.0f : gain;
        virt_x = std::clamp(virt_x + dx * gx, -1.0f, 1.0f);
        virt_y = std::clamp(virt_y + dy * gy, -1.0f, 1.0f);
    }

    if (!moving && std::abs(virt_x) < 0.002f) virt_x = 0.0f;
    if (!moving && std::abs(virt_y) < 0.002f) virt_y = 0.0f;
}
inline Report make_report_with_tilt(const std::array<float, ACTION_COUNT> &values, float tilt_x, float tilt_y,
                                    MotionMode mode, bool real_motion, const std::array<float, 3> &accel,
                                    const std::array<float, 3> &gyro, uint64_t now, MotionPulse &pulse) {
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

    float sx = std::isfinite(tilt_x) ? std::clamp(tilt_x, -1.0f, 1.0f) : 0.0f;
    float sy = std::isfinite(tilt_y) ? std::clamp(tilt_y, -1.0f, 1.0f) : 0.0f;
    float stick_s0 = sx * (sx < 0.f ? 512.f : 511.f);
    float stick_s1 = sy * (sy < 0.f ? 399.f : 624.f);
    float stick_s3 = sx * 123.f;

    // Physical sensor input (gyro + accelerometer)
    bool has_real = (mode != MotionMode::RightStick) && real_motion && finite_motion(accel, gyro);
    float base_s0 = 512.f, base_s1 = 399.f, base_s2 = 512.f, base_s3 = 512.f;
    if (has_real) {
        base_s0 = 512.f - accel[0] * (113.f / 9.80665f);
        base_s1 = 512.f - accel[1] * (113.f / 9.80665f);
        base_s2 = 512.f - accel[2] * (113.f / 9.80665f);
        base_s3 = 512.f + gyro[1] * (180.f / 3.14159265358979323846f) * (123.f / 90.f);
    }

    r.sensors[0] = sensor(base_s0 + stick_s0);
    r.sensors[1] = sensor(base_s1 + stick_s1);
    r.sensors[2] = sensor(base_s2);
    r.sensors[3] = sensor(base_s3 + stick_s3);
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
inline Report make_report(const std::array<float, ACTION_COUNT> &values, MotionMode mode, bool real_motion,
                          const std::array<float, 3> &accel, const std::array<float, 3> &gyro, uint64_t now,
                          MotionPulse &pulse) {
    float stick_x = values[R_RIGHT] - values[R_LEFT];
    float stick_y = values[R_DOWN] - values[R_UP];
    return make_report_with_tilt(values, stick_x, stick_y, mode, real_motion, accel, gyro, now, pulse);
}
} // namespace player
