#pragma once
#include "player_config.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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
// PS3 Sixaxis full-scale 1G range used by the game's sensor decoder (func_002E3374 / func_002E3930).
constexpr float SIXAXIS_ONE_G_COUNTS = 113.0f;
constexpr float SIXAXIS_YAW_COUNTS_PER_90DPS = 123.0f;
// Vertical throw/shake span on sensors[1] (rest 399 = -1.0G) for CShootControlBakuganTask::func_000E4804
// (triggers launch on Y < -1.0 && delta_Y < -1.0) with upward slew bounded below func_000C6F90's
// +1.10G/frame rolling jump threshold (~+0.57G per 33 ms frame).
constexpr float SIXAXIS_THROW_Y_COUNTS = 340.0f;
constexpr float SIXAXIS_SENSOR_Y_RISE_RATE = 5.5f;
// Soften binary DualShock 3 small motor relative to the large motor on modern host gamepads.
constexpr float SMALL_MOTOR_BALANCE = 0.70f;

inline void scale_rumble_motors(uint16_t raw_lo, uint16_t raw_hi, int strength_pct,
                                uint16_t &out_lo, uint16_t &out_hi) {
    const int clamped = std::clamp(strength_pct, 0, 100);
    if (clamped == 0) {
        out_lo = 0;
        out_hi = 0;
        return;
    }
    const float gain = static_cast<float>(clamped) / 100.0f;
    out_lo = static_cast<uint16_t>(std::clamp(std::lround(raw_lo * gain), 0l, 65535l));
    out_hi = static_cast<uint16_t>(
        std::clamp(std::lround(raw_hi * gain * SMALL_MOTOR_BALANCE), 0l, 65535l));
}
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
inline float step_sensor_y_tilt(float current, float target, float dt_sec) {
    if (!std::isfinite(current))
        current = 0.0f;
    if (!std::isfinite(target))
        target = 0.0f;
    target = std::clamp(target, -1.0f, 1.0f);
    if (!std::isfinite(dt_sec) || dt_sec <= 0.0f || target <= current)
        return target;
    const float next = std::min(target, current + SIXAXIS_SENSOR_Y_RISE_RATE * dt_sec);
    return (std::abs(target) < 1e-5f && std::abs(next) < 5e-4f) ? 0.0f : next;
}
struct SixaxisState {
    float tilt_x = 0.0f;
    float tilt_y = 0.0f;
    float stick_tilt_x = 0.0f;
    float stick_tilt_y = 0.0f;
    float sensor_y_tilt = 0.0f;
    float stick_sensor_y_tilt = 0.0f;
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
    float stick_target_x = 0.0f, stick_target_y = 0.0f;
    shape_sixaxis_2d(stick_x, stick_y, tilt_deadzone, cfg.sixaxis_curve * 0.40f, stick_target_x, stick_target_y);
    s.stick_tilt_x = std::clamp(ema_step(s.stick_tilt_x, stick_target_x, dt_sec, tau, 5.0f), -1.0f, 1.0f);
    s.stick_tilt_y = std::clamp(ema_step(s.stick_tilt_y, stick_target_y, dt_sec, tau, 5.0f), -1.0f, 1.0f);
    s.sensor_y_tilt = step_sensor_y_tilt(s.sensor_y_tilt, std::clamp(s.tilt_y, -1.0f, 1.0f), dt_sec);
    s.stick_sensor_y_tilt = step_sensor_y_tilt(s.stick_sensor_y_tilt, s.stick_tilt_y, dt_sec);
    out_tilt_x = std::clamp(s.tilt_x, -1.0f, 1.0f);
    out_tilt_y = std::clamp(s.tilt_y, -1.0f, 1.0f);
    out_accel = s.accel;
    if (cfg.gyro_invert_x)
        out_accel[0] = -out_accel[0];
    out_gyro = s.gyro;
}
// Integrate mouse pixel deltas into a symmetric leaky virtual stick matching the calibrated
// +-113-count (1.0G) Sixaxis steering and acceleration/brake range without triggering sensors[1] jumps.
inline void step_mouse_virtual_stick(float &virt_x, float &virt_y, int raw_dx, int raw_dy,
                                     float sensitivity, float dt_sec) {
    if (!std::isfinite(virt_x)) virt_x = 0.0f;
    if (!std::isfinite(virt_y)) virt_y = 0.0f;
    if (!std::isfinite(dt_sec) || dt_sec <= 0.0f) return;

    const float sens = std::clamp(sensitivity, 0.05f, 2.0f);
    const bool moving = (raw_dx != 0 || raw_dy != 0);
    const float decay = std::exp(-dt_sec / (moving ? 0.60f : 0.16f));
    virt_x *= decay;
    virt_y *= decay;

    if (moving) {
        constexpr float kBaseGain = 0.0160f;
        constexpr float kCounterSteerBoost = 2.8f;
        const float dx = static_cast<float>(raw_dx);
        const float dy = static_cast<float>(raw_dy);
        const float gain = sens * kBaseGain;
        const float gx = (dx * virt_x < 0.0f) ? gain * kCounterSteerBoost : gain;
        const float gy = (dy * virt_y < 0.0f) ? gain * kCounterSteerBoost : gain;
        virt_x = std::clamp(virt_x + dx * gx, -1.0f, 1.0f);
        virt_y = std::clamp(virt_y + dy * gy, -1.0f, 1.0f);
    }

    if (!moving && std::abs(virt_x) < 0.002f) virt_x = 0.0f;
    if (!moving && std::abs(virt_y) < 0.002f) virt_y = 0.0f;
}
inline Report make_report_with_tilt(const std::array<float, ACTION_COUNT> &values, float tilt_x, float tilt_y,
                                    MotionMode mode, bool real_motion, const std::array<float, 3> &accel,
                                    const std::array<float, 3> &gyro, uint64_t now, MotionPulse &pulse,
                                    bool gyro_invert_x = false, float tilt_scale = 1.0f,
                                    float sensor_y_tilt = std::numeric_limits<float>::quiet_NaN()) {
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
    if (has_real && mode == MotionMode::Sensors) {
        tilt_x = 0.0f;
        tilt_y = 0.0f;
        sensor_y_tilt = 0.0f;
    }
    const float sx = std::isfinite(tilt_x) ? std::clamp(tilt_x, -1.0f, 1.0f) : 0.0f;
    const float sy = std::isfinite(tilt_y) ? std::clamp(tilt_y, -1.0f, 1.0f) : 0.0f;
    const float sy_throw = std::isfinite(sensor_y_tilt) ? std::clamp(sensor_y_tilt, -1.0f, 1.0f) : sy;
    const float scale = std::isfinite(tilt_scale) ? std::clamp(tilt_scale, 0.1f, 5.0f) : 1.0f;
    float base_s0 = 512.f, base_s1 = 399.f, base_s2 = 512.f, base_s3 = 512.f;
    if (has_real) {
        float ax = gyro_invert_x ? -accel[0] : accel[0];
        base_s0 = 512.f - ax * (113.f / 9.80665f);
        base_s1 = 512.f - accel[1] * (113.f / 9.80665f);
        base_s2 = 512.f - accel[2] * (113.f / 9.80665f);
        base_s3 = 512.f + gyro[1] * (180.f / 3.14159265358979323846f) * (123.f / 90.f);
    }
    const auto apply_tilt = [scale](float base, float tilt) {
        base = std::isfinite(base) ? std::clamp(base, 0.0f, 1023.0f) : 512.0f;
        const float delta = static_cast<float>(std::lround(tilt * (SIXAXIS_ONE_G_COUNTS * scale)));
        return sensor(base + delta);
    };
    r.sensors[0] = apply_tilt(base_s0, sx);
    const float s1_base = std::isfinite(base_s1) ? std::clamp(base_s1, 0.0f, 1023.0f) : 399.0f;
    const float s1_delta = static_cast<float>(std::lround(sy_throw * SIXAXIS_THROW_Y_COUNTS));
    r.sensors[1] = sensor(s1_base + s1_delta);
    r.sensors[2] = apply_tilt(base_s2, sy);
    r.sensors[3] = sensor(base_s3 + sx * (SIXAXIS_YAW_COUNTS_PER_90DPS * scale));
    bool press = values[MOTION_THROW] > 0.25f;
    if (press && !pulse.held) {
        pulse.start = now;
        pulse.active = true;
    }
    pulse.held = press;
    if (pulse.active) {
        uint64_t elapsed = now - pulse.start;
        if (elapsed < 100) {
            r.sensors[1] = 59;
            r.sensors[2] = 1023;
        } else if (elapsed < 150) {
            r.sensors[1] = 229;
            r.sensors[2] = 0;
        } else if (elapsed < 200) {
            r.sensors[1] = 314;
            r.sensors[2] = 0;
        } else {
            pulse.active = false;
        }
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
    const float sensor_tilt_x = real ? (config.motion == MotionMode::Auto ? sixaxis.stick_tilt_x : 0.0f) : tilt_x;
    const float sensor_tilt_y = real ? (config.motion == MotionMode::Auto ? sixaxis.stick_tilt_y : 0.0f) : tilt_y;
    const float sensor_throw_y =
        real ? (config.motion == MotionMode::Auto ? sixaxis.stick_sensor_y_tilt : 0.0f) : sixaxis.sensor_y_tilt;
    Report report = make_report_with_tilt(values, sensor_tilt_x, sensor_tilt_y, config.motion, real,
                                          filtered_accel, filtered_gyro, now, pulse,
                                          false, config.gyro_sensitivity, sensor_throw_y);
    if (real) {
        report.rx = stick(std::max(0.0f, -tilt_x), std::max(0.0f, tilt_x));
        report.ry = stick(std::max(0.0f, -tilt_y), std::max(0.0f, tilt_y));
    }
    return report;
}
inline Report make_keyboard_mouse_report(const std::array<float, ACTION_COUNT> &values,
                                         float mouse_virt_x, float mouse_virt_y,
                                         const Config &config, float dt_sec, uint64_t now,
                                         SixaxisState &sixaxis, MotionPulse &pulse,
                                         bool active = true) {
    auto out_values = values;
    float tilt_x = 0.0f, tilt_y = 0.0f;
    std::array<float, 3> a{0.0f, 9.80665f, 0.0f}, g{}, filt_a{0.0f, 9.80665f, 0.0f}, filt_g{};
    if (active) {
        const float kb_tilt_x = out_values[R_RIGHT] - out_values[R_LEFT];
        const float kb_tilt_y = out_values[R_DOWN] - out_values[R_UP];
        const float mx = std::isfinite(mouse_virt_x) ? mouse_virt_x : 0.0f;
        const float my = std::isfinite(mouse_virt_y) ? mouse_virt_y : 0.0f;
        const float raw_tilt_x = std::clamp(kb_tilt_x + mx, -1.0f, 1.0f);
        const float raw_tilt_y = std::clamp(kb_tilt_y + my, -1.0f, 1.0f);
        step_sixaxis(sixaxis, raw_tilt_x, raw_tilt_y, 0.0f, false, a, g,
                     config, dt_sec, tilt_x, tilt_y, filt_a, filt_g);
        out_values[R_LEFT] = std::max(0.0f, -tilt_x);
        out_values[R_RIGHT] = std::max(0.0f, tilt_x);
        out_values[R_UP] = std::max(0.0f, -tilt_y);
        out_values[R_DOWN] = std::max(0.0f, tilt_y);
    } else {
        out_values[R_LEFT] = 0.0f;
        out_values[R_RIGHT] = 0.0f;
        out_values[R_UP] = 0.0f;
        out_values[R_DOWN] = 0.0f;
    }
    const MotionMode kb_mode = config.motion == MotionMode::Sensors ? MotionMode::Auto : config.motion;
    return make_report_with_tilt(out_values, tilt_x, tilt_y, kb_mode, false, filt_a, filt_g, now, pulse,
                                 false, config.gyro_sensitivity, active ? sixaxis.sensor_y_tilt : 0.0f);
}
inline Report make_report(const std::array<float, ACTION_COUNT> &values, MotionMode mode, bool real_motion,
                          const std::array<float, 3> &accel, const std::array<float, 3> &gyro, uint64_t now,
                          MotionPulse &pulse) {
    float stick_x = values[R_RIGHT] - values[R_LEFT];
    float stick_y = values[R_DOWN] - values[R_UP];
    return make_report_with_tilt(values, stick_x, stick_y, mode, real_motion, accel, gyro, now, pulse);
}
} // namespace player
