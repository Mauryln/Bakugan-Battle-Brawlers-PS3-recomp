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
// Preserve aiming speed around the whole circle and keep fine control near center.
inline void shape_aim_stick(float x, float y, float deadzone, float &out_x, float &out_y) {
    const float radius = std::hypot(x, y);
    deadzone = std::clamp(deadzone, 0.0f, 0.95f);
    if (!std::isfinite(radius) || radius <= deadzone) {
        out_x = out_y = 0.0f;
        return;
    }
    const float travel = std::clamp((radius - deadzone) / (1.0f - deadzone), 0.0f, 1.0f);
    constexpr float center_gain = 0.70f;
    const float scale = travel * (center_gain + (1.0f - center_gain) * travel) / radius;
    out_x = x * scale;
    out_y = y * scale;
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
    float roll_angle = 0.0f;
    float pitch_angle = 0.0f;
    float rest_pitch = 0.0f;
    float yaw_angle = 0.0f;
    float steady_sec = 0.0f;
    std::array<float, 3> accel{0.0f, 9.80665f, 0.0f};
    std::array<float, 3> gyro{0.0f, 0.0f, 0.0f};
    bool accel_seeded = false;
};
inline void step_sixaxis(SixaxisState &s, float raw_tilt_x, float raw_tilt_y, float tilt_deadzone,
                         bool has_real, const std::array<float, 3> &raw_accel,
                         const std::array<float, 3> &raw_gyro, const Config &cfg, float dt_sec,
                         float &out_tilt_x, float &out_tilt_y, std::array<float, 3> &out_accel,
                         std::array<float, 3> &out_gyro) {
    float tau = std::clamp(cfg.sixaxis_smoothing, 0.0f, 1.0f) * 0.022f;
    float sens = std::clamp(cfg.gyro_sensitivity, 0.1f, 5.0f);
    float gyro_tilt_x = 0.0f, gyro_tilt_y = 0.0f;
    bool real_active = has_real && cfg.motion != MotionMode::RightStick && finite_motion(raw_accel, raw_gyro);

    if (real_active) {
        // SDL gamepad gravity angles: tilt right (+roll) yields accel[0] < 0 and gyro[2] < 0;
        // tilt nose down/forward (-pitch) yields accel[2] > 0 and gyro[0] < 0.
        float accel_roll = std::atan2(-raw_accel[0], std::max(0.5f, std::hypot(raw_accel[1], raw_accel[2])));
        float accel_pitch = -std::atan2(raw_accel[2], std::max(0.5f, std::hypot(raw_accel[0], raw_accel[1])));

        if (!s.accel_seeded) {
            s.accel = raw_accel;
            s.roll_angle = accel_roll;
            s.pitch_angle = accel_pitch;
            s.rest_pitch = std::clamp(accel_pitch, -0.60f, 0.60f);
            s.yaw_angle = 0.0f;
            s.steady_sec = 0.0f;
            s.accel_seeded = true;
        } else {
            for (int i = 0; i < 3; ++i)
                s.accel[i] = ema_step(s.accel[i], raw_accel[i], dt_sec, tau * 0.6f, 2.5f);
        }

        float dz_pitch = raw_gyro[0], dz_yaw = raw_gyro[1], dz_roll = raw_gyro[2];
        float speed = std::hypot(dz_yaw, dz_pitch);
        float gz_dz = std::max(0.0f, cfg.gyro_deadzone);
        if (speed <= gz_dz || speed < 1e-6f) {
            dz_yaw = 0.0f;
            dz_pitch = 0.0f;
        } else {
            float scale = (speed - gz_dz) / speed;
            dz_yaw *= scale;
            dz_pitch *= scale;
        }
        if (std::abs(dz_roll) <= gz_dz)
            dz_roll = 0.0f;
        else
            dz_roll -= std::copysign(gz_dz, dz_roll);

        if (dt_sec > 0.0f) {
            float accel_mag = std::hypot(raw_accel[0], std::hypot(raw_accel[1], raw_accel[2]));
            float g_err = std::abs(accel_mag - 9.80665f) / 9.80665f;
            float grav_weight = (accel_mag > 2.0f) ? std::clamp(1.0f - g_err * 1.5f, 0.05f, 1.0f) : 0.0f;
            constexpr float kTauCf = 0.25f;
            float alpha_cf = kTauCf / (kTauCf + dt_sec * grav_weight);

            s.roll_angle = std::clamp(
                alpha_cf * (s.roll_angle + (-dz_roll) * dt_sec) + (1.0f - alpha_cf) * accel_roll,
                -1.5f, 1.5f);
            s.pitch_angle = std::clamp(
                alpha_cf * (s.pitch_angle + dz_pitch * dt_sec) + (1.0f - alpha_cf) * accel_pitch,
                -1.5f, 1.5f);

            // Track resting hand pitch only when controller is steady and horizontally level.
            bool steady_level = (std::abs(raw_gyro[0]) <= gz_dz + 0.02f) &&
                                (std::abs(raw_gyro[1]) <= gz_dz + 0.02f) &&
                                (std::abs(raw_gyro[2]) <= gz_dz + 0.02f) &&
                                (std::abs(s.roll_angle) < 0.12f);
            if (steady_level) {
                s.steady_sec += dt_sec;
                if (s.steady_sec > 0.6f)
                    s.rest_pitch = ema_step(s.rest_pitch, std::clamp(s.pitch_angle, -0.60f, 0.60f), dt_sec, 3.0f);
            } else {
                s.steady_sec = 0.0f;
            }

            constexpr float kTauYawDecay = 0.26f;
            s.yaw_angle = std::clamp(
                s.yaw_angle * std::exp(-dt_sec / kTauYawDecay) + (-dz_yaw) * dt_sec,
                -1.2f, 1.2f);
        }

        float sign_x = cfg.gyro_invert_x ? -1.0f : 1.0f;
        float sign_y = cfg.gyro_invert_y ? -1.0f : 1.0f;
        float steer_rate = (-dz_yaw - dz_roll) * sign_x;
        s.gyro[0] = ema_step(s.gyro[0], dz_pitch * sign_y * sens, dt_sec, tau * 0.5f, 4.0f);
        s.gyro[1] = ema_step(s.gyro[1], steer_rate * sens, dt_sec, tau * 0.5f, 4.0f);
        s.gyro[2] = ema_step(s.gyro[2], dz_roll * sens, dt_sec, tau * 0.5f, 4.0f);

        // Fuse gravity-referenced tilt angles with rate feedforward so holds stay locked and flicks snap.
        constexpr float kFullTiltRad = 0.48f;
        constexpr float kRollRateFF = 0.075f;
        constexpr float kYawRateFF = 0.13f;
        constexpr float kPitchRateFF = 0.085f;
        float x_ang = (s.roll_angle + (-dz_roll) * kRollRateFF) + (s.yaw_angle + (-dz_yaw) * kYawRateFF);
        float y_ang = (s.pitch_angle - s.rest_pitch) + dz_pitch * kPitchRateFF;

        gyro_tilt_x = std::clamp((x_ang * sign_x * sens) / kFullTiltRad, -1.0f, 1.0f);
        gyro_tilt_y = std::clamp((y_ang * sign_y * sens) / kFullTiltRad, -1.0f, 1.0f);

        constexpr float kGyroTiltMicroDz = 0.035f;
        float gr = std::hypot(gyro_tilt_x, gyro_tilt_y);
        if (gr <= kGyroTiltMicroDz || gr < 1e-6f) {
            gyro_tilt_x = 0.0f;
            gyro_tilt_y = 0.0f;
        } else {
            float gscale = std::min(1.0f, (gr - kGyroTiltMicroDz) / (1.0f - kGyroTiltMicroDz)) / gr;
            gyro_tilt_x *= gscale;
            gyro_tilt_y *= gscale;
        }
    } else {
        s.accel_seeded = false;
        s.roll_angle = 0.0f;
        s.pitch_angle = 0.0f;
        s.rest_pitch = 0.0f;
        s.yaw_angle = 0.0f;
        s.steady_sec = 0.0f;
        s.accel[0] = ema_step(s.accel[0], 0.0f, dt_sec, tau * 0.6f, 2.0f);
        s.accel[1] = ema_step(s.accel[1], 9.80665f, dt_sec, tau * 0.6f, 2.0f);
        s.accel[2] = ema_step(s.accel[2], 0.0f, dt_sec, tau * 0.6f, 2.0f);
        for (int i = 0; i < 3; ++i)
            s.gyro[i] = ema_step(s.gyro[i], 0.0f, dt_sec, tau * 0.5f, 3.0f);
    }

    float stick_x = (cfg.motion == MotionMode::Sensors && real_active) ? 0.0f : raw_tilt_x;
    float stick_y = (cfg.motion == MotionMode::Sensors && real_active) ? 0.0f : raw_tilt_y;
    // Keep gyro correction on each axis while giving the stick control at its end stop.
    float combined_x = stick_x + (1.0f - std::abs(stick_x)) * gyro_tilt_x;
    float combined_y = stick_y + (1.0f - std::abs(stick_y)) * gyro_tilt_y;
    float target_x = 0.0f, target_y = 0.0f;
    shape_sixaxis_2d(combined_x, combined_y, tilt_deadzone, cfg.sixaxis_curve * 0.40f, target_x, target_y);
    s.tilt_x = ema_step(s.tilt_x, target_x, dt_sec, tau, 5.0f);
    s.tilt_y = ema_step(s.tilt_y, target_y, dt_sec, tau, 5.0f);
    out_tilt_x = std::clamp(s.tilt_x, -1.0f, 1.0f);
    out_tilt_y = std::clamp(s.tilt_y, -1.0f, 1.0f);
    out_accel = s.accel;
    if (cfg.gyro_invert_x)
        out_accel[0] = -out_accel[0];
    out_gyro = s.gyro;
}
// Integrate mouse pixel deltas into a leaky virtual stick: captures 1px slow sweeps,
// turns and counter-steers crisply, holds steady across sweeps, and caps abrupt forward
// flicks so mouse pushes accelerate smoothly without triggering Sixaxis jump spikes.
inline void step_mouse_virtual_stick(float &virt_x, float &virt_y, int raw_dx, int raw_dy,
                                     float sensitivity, float dt_sec) {
    if (!std::isfinite(virt_x)) virt_x = 0.0f;
    if (!std::isfinite(virt_y)) virt_y = 0.0f;
    if (!std::isfinite(dt_sec) || dt_sec <= 0.0f) return;

    float sens = std::clamp(sensitivity, 0.05f, 2.0f);
    bool moving = (raw_dx != 0 || raw_dy != 0);
    float decay = std::exp(-dt_sec / (moving ? 0.60f : 0.16f));
    virt_x *= decay;
    virt_y *= decay;

    if (moving) {
        float dx = (float)raw_dx;
        float dy = (float)raw_dy;
        float gain_x = sens * 0.0125f;
        float gain_y = sens * 0.0110f;
        float gx = (dx * virt_x < 0.0f) ? gain_x * 2.8f : gain_x;
        float gy = (dy * virt_y < 0.0f) ? gain_y * 2.5f : gain_y;
        virt_x = std::clamp(virt_x + dx * gx, -1.0f, 1.0f);

        float delta_y = dy * gy;
        if (delta_y < 0.0f) {
            float max_fwd = 3.8f * dt_sec;
            delta_y = std::max(delta_y, -max_fwd);
        }
        virt_y = std::clamp(virt_y + delta_y, -1.0f, 1.0f);
    }

    if (!moving && std::abs(virt_x) < 0.002f) virt_x = 0.0f;
    if (!moving && std::abs(virt_y) < 0.002f) virt_y = 0.0f;
}
inline Report make_report_with_tilt(const std::array<float, ACTION_COUNT> &values, float tilt_x, float tilt_y,
                                    MotionMode mode, bool real_motion, const std::array<float, 3> &accel,
                                    const std::array<float, 3> &gyro, uint64_t now, MotionPulse &pulse,
                                    bool gyro_invert_x = false) {
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

    const bool has_real = mode != MotionMode::RightStick && real_motion && finite_motion(accel, gyro);
    // Hybrid contributes the physical stick to the sensor words. The fused tilt also
    // contains sensor motion, which the filtered accelerometer and gyro already report.
    if (has_real) {
        tilt_x = 0.0f;
        tilt_y = 0.0f;
        if (mode == MotionMode::Auto) {
            tilt_x = values[R_RIGHT] - values[R_LEFT];
            tilt_y = values[R_DOWN] - values[R_UP];
        }
    }
    const float sx = std::isfinite(tilt_x) ? std::clamp(tilt_x, -1.0f, 1.0f) : 0.0f;
    const float sy = std::isfinite(tilt_y) ? std::clamp(tilt_y, -1.0f, 1.0f) : 0.0f;
    float base_s0 = 512.f, base_s1 = 399.f, base_s2 = 512.f, base_s3 = 512.f;
    if (has_real) {
        float ax = gyro_invert_x ? -accel[0] : accel[0];
        base_s0 = 512.f - ax * (113.f / 9.80665f);
        base_s1 = 512.f - accel[1] * (113.f / 9.80665f);
        base_s2 = 512.f - accel[2] * (113.f / 9.80665f);
        base_s3 = 512.f + gyro[1] * (180.f / 3.14159265358979323846f) * (123.f / 90.f);
    }
    const auto apply_tilt = [](float base, float tilt) {
        base = std::clamp(base, 0.0f, 1023.0f);
        return sensor(base + tilt * (tilt < 0.f ? base : 1023.f - base));
    };
    r.sensors[0] = apply_tilt(base_s0, sx);
    r.sensors[1] = apply_tilt(base_s1, sy);
    r.sensors[2] = sensor(base_s2);
    r.sensors[3] = sensor(base_s3 + sx * 123.f);
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
inline Report make_gamepad_report(const std::array<float, ACTION_COUNT> &values, const Config &config,
                                  bool has_sensors, const std::array<float, 3> &accel,
                                  const std::array<float, 3> &gyro, float dt_sec, uint64_t now,
                                  SixaxisState &sixaxis, MotionPulse &pulse) {
    const bool real = has_sensors && config.motion != MotionMode::RightStick && finite_motion(accel, gyro);
    const float stick_x = values[R_RIGHT] - values[R_LEFT];
    const float stick_y = values[R_DOWN] - values[R_UP];
    float tilt_x = 0.0f, tilt_y = 0.0f;
    std::array<float, 3> filtered_accel{}, filtered_gyro{};
    step_sixaxis(sixaxis, stick_x, stick_y, 0.0f, real, accel, gyro, config, dt_sec,
                 tilt_x, tilt_y, filtered_accel, filtered_gyro);
    Report report = make_report_with_tilt(values, tilt_x, tilt_y, config.motion, real,
                                          filtered_accel, filtered_gyro, now, pulse);
    if (real) {
        report.rx = stick(std::max(0.0f, -tilt_x), std::max(0.0f, tilt_x));
        report.ry = stick(std::max(0.0f, -tilt_y), std::max(0.0f, tilt_y));
    }
    return report;
}
inline Report make_report(const std::array<float, ACTION_COUNT> &values, MotionMode mode, bool real_motion,
                          const std::array<float, 3> &accel, const std::array<float, 3> &gyro, uint64_t now,
                          MotionPulse &pulse) {
    float stick_x = values[R_RIGHT] - values[R_LEFT];
    float stick_y = values[R_DOWN] - values[R_UP];
    return make_report_with_tilt(values, stick_x, stick_y, mode, real_motion, accel, gyro, now, pulse);
}
} // namespace player
