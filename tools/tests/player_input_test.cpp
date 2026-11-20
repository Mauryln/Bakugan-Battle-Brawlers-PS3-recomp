#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "fps_cap.h"
#include "player_config.h"
#include "gamepad_devices.h"
#include "input_model.h"
#include <filesystem>
#include <cstdio>
#include <limits>

static int failures = 0;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                        \
            ++failures;                                                                                      \
        }                                                                                                    \
    } while (0)
int main() {
    using namespace player;
    auto c = defaults();
    CHECK(c.gamepad[CROSS].kind == BindingKind::Button && c.gamepad[CROSS].code == SDL_GAMEPAD_BUTTON_SOUTH);
    Binding b;
    CHECK(!parse_binding(L"key:999", b));
    CHECK(!parse_binding(L"axis+:6", b));
    CHECK(!parse_binding(L"mouse:99", b));
    CHECK(!parse_binding(L"key:12oops", b));
    CHECK(parse_binding(L"axis-:3", b) && b.kind == BindingKind::AxisNegative && b.code == 3);
    auto path = std::filesystem::temp_directory_path() / L"bakugan-player-test-\u00f1.ini";
    std::filesystem::remove(path);
    c.language = L"es";
    c.output = L"1440p";
    c.cap = L"60";
    c.aa_mode = L"off";
    c.sharpness = 1.2f;
    c.aniso = 4;
    c.deadzone = 0.22f;
    c.keyboard[CROSS] = {BindingKind::Key, 'B'};
    c.gamepad[CROSS] = {BindingKind::Button, SDL_GAMEPAD_BUTTON_WEST};
    c.device_guid = "test-guid";
    c.device_serial = "serial";
    c.motion = MotionMode::RightStick;
    std::wstring error;
    CHECK(save(c, path.wstring(), &error));
    Config loaded;
    CHECK(load(loaded, path.wstring(), &error));
    CHECK(loaded.language == c.language && loaded.output == c.output && loaded.cap == c.cap);
    CHECK(loaded.aa_mode == L"off" && std::abs(loaded.sharpness - 1.2f) < 0.001f && loaded.aniso == 4);
    CHECK(std::abs(loaded.deadzone - c.deadzone) < 0.001f && loaded.motion == c.motion);
    CHECK(loaded.keyboard[CROSS].code == 'B' && loaded.gamepad[CROSS].code == SDL_GAMEPAD_BUTTON_WEST);
    CHECK(loaded.device_guid == c.device_guid && loaded.device_serial == c.device_serial);
    CHECK(WritePrivateProfileStringW(L"Graphics", L"Sharpness", L"nan", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Graphics", L"Output", L"nonsense", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Graphics", L"AAMode", L"invalid", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Keyboard", L"14", L"key:9999", path.c_str()));
    CHECK(load(loaded, path.wstring(), &error));
    CHECK(loaded.sharpness == 0.6f && loaded.output == L"720p" && loaded.aa_mode == L"smaa");
    CHECK(loaded.keyboard[CROSS].code == 'X');

    // Aliases for aa_mode, fsr_sharpness, and anisotropic in player.ini
    auto alias_path = std::filesystem::temp_directory_path() / L"bakugan-gfx-alias-test.ini";
    std::filesystem::remove(alias_path);
    CHECK(WritePrivateProfileStringW(L"Player", L"Version", L"1", alias_path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Graphics", L"aa_mode", L"off", alias_path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Graphics", L"fsr_sharpness", L"high", alias_path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Graphics", L"anisotropic", L"off", alias_path.c_str()));
    Config alias_cfg;
    CHECK(load(alias_cfg, alias_path.wstring(), &error));
    CHECK(alias_cfg.aa_mode == L"off" && std::abs(alias_cfg.sharpness - 0.25f) < 0.001f && alias_cfg.aniso == 1);
    std::filesystem::remove(alias_path);
    CHECK(WritePrivateProfileStringW(L"Player", L"Version", L"999", path.c_str()));
    CHECK(!load(loaded, path.wstring(), &error));
    c.language = L"auto";
    SetEnvironmentVariableW(L"PAD_AUTOPRESS", L"1");
    SetEnvironmentVariableW(L"PS3_LANG", L"de");
    apply_environment(c, path.wstring());
    wchar_t env[128];
    CHECK(!GetEnvironmentVariableW(L"PAD_AUTOPRESS", env, 128));
    CHECK(!GetEnvironmentVariableW(L"PS3_LANG", env, 128));
    CHECK(GetEnvironmentVariableW(L"PS3_OUTPUT", env, 128) && std::wstring(env) == L"1440p");
    CHECK(GetEnvironmentVariableW(L"PS3_AA", env, 128) && std::wstring(env) == L"off");
    CHECK(GetEnvironmentVariableW(L"PS3_ANISO", env, 128) && std::wstring(env) == L"4");
    SetEnvironmentVariableW(L"PS3_DEBUG", L"1");
    SetEnvironmentVariableW(L"PAD_REPLAY", L"test.padrec");
    SetEnvironmentVariableW(L"PS3_SAVEDATA_CHOICE", L"BLES00758-PLAYER-DATA-00");
    apply_environment(c, path.wstring());
    CHECK(GetEnvironmentVariableW(L"PAD_REPLAY", env, 128) && std::wstring(env) == L"test.padrec");
    CHECK(GetEnvironmentVariableW(L"PS3_SAVEDATA_CHOICE", env, 128));
    SetEnvironmentVariableW(L"PS3_DEBUG", nullptr);
    apply_environment(c, path.wstring());
    CHECK(!GetEnvironmentVariableW(L"PAD_REPLAY", env, 128));
    CHECK(!GetEnvironmentVariableW(L"PS3_SAVEDATA_CHOICE", env, 128));
    std::array<float, ACTION_COUNT> v{};
    std::array<float, 3> a{0, 9.80665f, 0}, g{};
    MotionPulse pulse;
    auto r = make_report(v, MotionMode::Auto, true, a, g, 1000, pulse);
    CHECK(r.buttons == 0 && r.lx == 128 && r.ly == 128 && r.rx == 128 && r.ry == 128);
    CHECK(r.sensors[0] == 512 && r.sensors[1] == 399 && r.sensors[2] == 512 && r.sensors[3] == 512);
    v[CROSS] = 1;
    v[L_RIGHT] = 1;
    v[L_UP] = 1;
    v[L2] = 0.5f;
    g[1] = 3.14159265358979323846f / 2;
    r = make_report(v, MotionMode::Auto, true, a, g, 1000, pulse);
    CHECK((r.buttons & (1 << 14)) && (r.buttons & (1 << 8)) && r.l2 == 128 && r.lx == 255 && r.ly == 0);
    CHECK(r.sensors[3] == 635);
    v[R_LEFT] = 1;
    v[R_DOWN] = 1;
    g[0] = std::numeric_limits<float>::quiet_NaN();
    r = make_report(v, MotionMode::Auto, true, a, g, 1000, pulse);
    CHECK(r.sensors[0] == 0 && r.sensors[1] == 1023);
    v[MOTION_THROW] = 1;
    CHECK(make_report(v, MotionMode::RightStick, false, a, g, 1000, pulse).sensors[2] == 1023);
    CHECK(make_report(v, MotionMode::RightStick, false, a, g, 1100, pulse).sensors[2] == 0);
    CHECK(make_report(v, MotionMode::RightStick, false, a, g, 1200, pulse).sensors[2] == 512);
    CHECK(make_report(v, MotionMode::RightStick, false, a, g, 1400, pulse).sensors[2] == 512);
    v[MOTION_THROW] = 0;
    make_report(v, MotionMode::RightStick, false, a, g, 1500, pulse);
    v[MOTION_THROW] = 1;
    CHECK(make_report(v, MotionMode::RightStick, false, a, g, 1600, pulse).sensors[2] == 1023);
    float sx = 0.f, sy = 0.f;
    shape_stick_2d(0.10f, -0.10f, 0.15f, sx, sy);
    CHECK(sx == 0.f && sy == 0.f);
    shape_stick_2d(0.85f, 0.08f, 0.15f, sx, sy);
    CHECK(sx > 0.99f && sy > 0.08f); // radial deadzone must not snap slight tilt onto cardinal axis
    shape_stick_2d(0.60f, -0.60f, 0.15f, sx, sy);
    CHECK(sx > 0.99f && sy < -0.99f); // diagonal rim (PS4/Switch circular gate) expands to full corner
    for (int k = 0; k < 16; ++k) {
        float ang = (float)k * (2.f * 3.14159265f / 16.f);
        float rx = 0.85f * std::cos(ang), ry = 0.85f * std::sin(ang);
        shape_stick_2d(rx, ry, 0.15f, sx, sy);
        CHECK(std::max(std::abs(sx), std::abs(sy)) > 0.99f);
        CHECK(std::abs(sx * ry - sy * rx) < 1e-4f);
    }

    // Sixaxis radial deadzone + smooth response curve + NaN safety
    shape_sixaxis_2d(0.10f, -0.10f, 0.15f, 0.80f, sx, sy);
    CHECK(sx == 0.f && sy == 0.f);
    shape_sixaxis_2d(0.85f, 0.08f, 0.15f, 0.50f, sx, sy);
    CHECK(sx > 0.5f && sy > 0.04f && std::abs(sx * 0.08f - sy * 0.85f) < 1e-4f);
    shape_sixaxis_2d(0.25f, 0.0f, 0.0f, 0.80f, sx, sy);
    CHECK(sx > 0.05f && sx < 0.10f); // fine control near center
    shape_sixaxis_2d(-1.0f, 1.0f, 0.15f, 0.80f, sx, sy);
    CHECK(sx < -0.999f && sy > 0.999f); // preserves full corner travel
    shape_sixaxis_2d(std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.15f, 0.5f, sx, sy);
    CHECK(sx == 0.f && sy == 0.f);

    // FPS-independent EMA step response, monotonic rest convergence, and zero drift
    Config smooth_cfg = defaults();
    apply_sixaxis_profile(smooth_cfg, SixaxisProfile::Smooth);
    CHECK(smooth_cfg.sixaxis_profile == SixaxisProfile::Smooth);
    CHECK(save(smooth_cfg, path.wstring(), &error));
    Config reloaded;
    CHECK(load(reloaded, path.wstring(), &error));
    CHECK(reloaded.sixaxis_profile == SixaxisProfile::Smooth);
    CHECK(std::abs(reloaded.sixaxis_curve - 0.80f) < 0.001f);
    CHECK(std::abs(reloaded.sixaxis_smoothing - 0.75f) < 0.001f);

    // Backwards compatibility: missing Sixaxis keys fall back to Standard defaults
    auto legacy_path = std::filesystem::temp_directory_path() / L"bakugan-legacy-test.ini";
    std::filesystem::remove(legacy_path);
    CHECK(WritePrivateProfileStringW(L"Player", L"Version", L"1", legacy_path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Input", L"Deadzone", L"0.22", legacy_path.c_str()));
    Config legacy_cfg;
    CHECK(load(legacy_cfg, legacy_path.wstring(), &error));
    CHECK(legacy_cfg.sixaxis_profile == SixaxisProfile::Standard);
    CHECK(std::abs(legacy_cfg.deadzone - 0.22f) < 0.001f);
    CHECK(std::abs(legacy_cfg.sixaxis_smoothing - 0.45f) < 0.001f);
    std::filesystem::remove(legacy_path);

    SixaxisState state_fast_hz, state_slow_hz;
    float tx_fast = 0.f, ty_fast = 0.f, tx_slow = 0.f, ty_slow = 0.f;
    std::array<float, 3> fa{0.f, 9.80665f, 0.f}, fg{0.f, 0.03f, 0.f}; // gyro inside deadzone (0.05)
    std::array<float, 3> out_a{}, out_g{};
    for (int i = 0; i < 40; ++i)
        step_sixaxis(state_fast_hz, 0.6f, 0.0f, 0.0f, true, fa, fg, smooth_cfg, 0.005f, tx_fast, ty_fast, out_a, out_g);
    for (int i = 0; i < 10; ++i)
        step_sixaxis(state_slow_hz, 0.6f, 0.0f, 0.0f, true, fa, fg, smooth_cfg, 0.020f, tx_slow, ty_slow, out_a, out_g);
    CHECK(std::isfinite(tx_fast) && std::isfinite(tx_slow));
    CHECK(std::abs(tx_fast - tx_slow) < 0.03f);
    CHECK(out_g[1] == 0.0f); // resting gyro drift inside deadzone is suppressed

    // Monotonic release to neutral (512, 399, 512, 512) without overshoot or drift
    float prev_tx = tx_fast;
    for (int i = 0; i < 60; ++i) {
        step_sixaxis(state_fast_hz, 0.0f, 0.0f, 0.0f, true, fa, fg, smooth_cfg, 0.010f, tx_fast, ty_fast, out_a, out_g);
        CHECK(tx_fast >= 0.0f && tx_fast <= prev_tx + 1e-6f);
        prev_tx = tx_fast;
    }
    auto rest_rep = make_report_with_tilt(std::array<float, ACTION_COUNT>{}, tx_fast, ty_fast,
                                          MotionMode::Auto, true, out_a, out_g, 2000, pulse);
    CHECK(rest_rep.sensors[0] == 512 && rest_rep.sensors[1] == 399 &&
          rest_rep.sensors[2] == 512 && rest_rep.sensors[3] == 512);

    // Extreme values & NaNs stay within [0, 1023] and respect MotionMode isolation
    std::array<float, ACTION_COUNT> ext_v{};
    ext_v[R_RIGHT] = 1.0f;
    std::array<float, 3> ext_a{100.f, -100.f, 100.f}, ext_g{-50.f, 50.f, 50.f};
    for (MotionMode m : {MotionMode::Auto, MotionMode::Sensors, MotionMode::RightStick}) {
        auto er = make_report_with_tilt(ext_v, 2.0f, -2.0f, m, true, ext_a, ext_g, 3000, pulse);
        for (int s = 0; s < 4; ++s)
            CHECK(er.sensors[s] <= 1023);
    }
    Config mode_cfg = defaults();
    mode_cfg.motion = MotionMode::Sensors;
    SixaxisState sensors_state;
    float stx = 0.f, sty = 0.f;
    for (int i = 0; i < 20; ++i)
        step_sixaxis(sensors_state, 1.0f, -1.0f, 0.0f, true,
                     std::array<float, 3>{0.f, 9.80665f, 0.f},
                     std::array<float, 3>{0.f, 0.f, 0.f}, mode_cfg, 0.020f, stx, sty, out_a, out_g);
    CHECK(stx == 0.0f && sty == 0.0f); // Sensors mode ignores right stick
    for (int i = 0; i < 20; ++i)
        step_sixaxis(sensors_state, 0.0f, 0.0f, 0.0f, true,
                     std::array<float, 3>{0.f, 9.80665f, 0.f},
                     std::array<float, 3>{0.f, -2.0f, 0.f}, mode_cfg, 0.020f, stx, sty, out_a, out_g);
    CHECK(stx > 0.90f); // physical gyro drives tilt_x (+right)
    mode_cfg.motion = MotionMode::RightStick;
    SixaxisState stick_only_state;
    for (int i = 0; i < 20; ++i)
        step_sixaxis(stick_only_state, 0.0f, 0.0f, 0.0f, true,
                     std::array<float, 3>{0.f, 9.80665f, 0.f},
                     std::array<float, 3>{0.f, -2.0f, 0.f}, mode_cfg, 0.020f, stx, sty, out_a, out_g);
    CHECK(stx == 0.0f && sty == 0.0f); // RightStick mode ignores physical gyro

    // Mouse leaky virtual stick: ignores 1px jitter, no 1/dt spike on short ticks, smooth return to 0
    float mvx = 0.f, mvy = 0.f;
    step_mouse_virtual_stick(mvx, mvy, 1, 0, 0.5f, 0.008f);
    CHECK(mvx == 0.0f && mvy == 0.0f);
    step_mouse_virtual_stick(mvx, mvy, 0, -6, 0.5f, 0.001f);
    CHECK(mvy < 0.0f && mvy > -0.05f); // short tick does not spike to jump threshold
    for (int i = 0; i < 15; ++i)
        step_mouse_virtual_stick(mvx, mvy, 12, 0, 0.5f, 0.008f);
    CHECK(mvx > 0.40f && mvx <= 1.0f);
    float peak_x = mvx;
    step_mouse_virtual_stick(mvx, mvy, -12, 0, 0.5f, 0.008f); // counter-steer cuts turn faster
    CHECK(mvx < peak_x - 0.05f);
    for (int i = 0; i < 200; ++i)
        step_mouse_virtual_stick(mvx, mvy, 0, 0, 0.5f, 0.010f);
    CHECK(mvx == 0.0f && mvy == 0.0f);
    Gamepads devices;
    std::string input_error;
    CHECK(devices.init(input_error));
    for (const auto &d : devices.devices())
        std::printf("DEVICE %s gyro=%d accel=%d\n", d.name.c_str(), d.gyro, d.accel);
    auto device = devices.selected(defaults());
    if (device) {
        std::array<float, 3> accel{}, gyro{};
        bool motion = devices.motion(device, accel, gyro);
        for (int i = 0; i < 50; ++i) {
            SDL_UpdateGamepads();
            Sleep(10);
            motion = devices.motion(device, accel, gyro);
        }
        std::printf("SENSORS read=%d accel=(%.3f,%.3f,%.3f) gyro=(%.3f,%.3f,%.3f)\n", motion, accel[0],
                    accel[1], accel[2], gyro[0], gyro[1], gyro[2]);
    }
    devices.close(true);
    std::filesystem::remove(path);

    // fps_cap_step: VSync active subtracts ~1.5 ms wake margin without accelerating next; VSync off uses exact interval
    {
        constexpr double interval = 1.0 / 30.0;
        double next_on = 0.0;
        const double wait_on = bakugan::fps_cap_step(10.0, interval, true, &next_on);
        CHECK(std::abs(wait_on - (interval - 0.0015)) < 1e-9);
        CHECK(std::abs(next_on - (10.0 + interval)) < 1e-9);

        double next_off = 0.0;
        const double wait_off = bakugan::fps_cap_step(10.0, interval, false, &next_off);
        CHECK(std::abs(wait_off - interval) < 1e-9);
        CHECK(std::abs(next_off - (10.0 + interval)) < 1e-9);

        // Simulate 5 frames on a 144 Hz vblank grid with 0.8 ms post-wait work before Present(1, 0):
        // margin allows 5,5,5,5,4 = 24 vblanks over 5 frames (exact 30.00 FPS vs 28.80 FPS without margin).
        constexpr double vblank = 1.0 / 144.0;
        double now = 100.0;
        double next = 0.0;
        int total_vblanks = 0;
        for (int f = 0; f < 5; ++f) {
            const double w = bakugan::fps_cap_step(now, interval, true, &next);
            const double present_arrival = now + w + 0.0008;
            const int vb = static_cast<int>(std::ceil((present_arrival - 100.0) / vblank - 1e-9));
            total_vblanks += (vb - total_vblanks);
            now = 100.0 + total_vblanks * vblank;
        }
        CHECK(total_vblanks == 24);

        // Long stall resets burst debt
        double next_stall = 10.0;
        CHECK(bakugan::fps_cap_step(10.5, interval, true, &next_stall) == 0.0);
        CHECK(std::abs(next_stall - 10.5) < 1e-9);
    }

    std::printf("Player config / input model: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
