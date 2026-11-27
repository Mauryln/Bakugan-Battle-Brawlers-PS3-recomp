#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "fps_cap.h"
#include "savedata_gdi_cache.h"
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
    CHECK(c.frame_gen == L"off");
    CHECK(c.motion == MotionMode::Auto);
    CHECK(c.sixaxis_profile == SixaxisProfile::Fast);
    CHECK(c.deadzone == 0.10f && c.gyro_sensitivity == 1.40f && c.sixaxis_smoothing == 0.15f);
    CHECK(c.vibration_strength == 80);
    CHECK(c.button_prompts == L"auto");
    CHECK(!has_input_overrides(c));
    c.vibration_strength = 60;
    CHECK(has_input_overrides(c));
    c.vibration_strength = 80;
    c.button_prompts = L"xbox";
    CHECK(has_input_overrides(c));
    c.button_prompts = L"auto";
    CHECK(!has_input_overrides(c));
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
    c.display_mode = L"fullscreen";
    c.aa_mode = L"off";
    c.frame_gen = L"fsr3";
    c.sharpness = 1.2f;
    c.aniso = 4;
    c.deadzone = 0.22f;
    c.keyboard[CROSS] = {BindingKind::Key, 'B'};
    c.gamepad[CROSS] = {BindingKind::Button, SDL_GAMEPAD_BUTTON_WEST};
    c.device_guid = "test-guid";
    c.device_serial = "serial";
    c.motion = MotionMode::RightStick;
    c.vibration_strength = 45;
    std::wstring error;
    CHECK(save(c, path.wstring(), &error));
    wchar_t fps[16];
    CHECK(GetPrivateProfileStringW(L"Graphics", L"FPS", L"", fps, 16, path.c_str()) &&
          std::wstring(fps) == L"30");
    Config loaded;
    CHECK(load(loaded, path.wstring(), &error));
    CHECK(loaded.vibration_strength == 45);
    CHECK(WritePrivateProfileStringW(L"Input", L"VibrationStrength", L"-5", path.c_str()));
    CHECK(load(loaded, path.wstring(), &error) && loaded.vibration_strength == 0);
    CHECK(WritePrivateProfileStringW(L"Input", L"VibrationStrength", L"250", path.c_str()));
    CHECK(load(loaded, path.wstring(), &error) && loaded.vibration_strength == 100);
    CHECK(WritePrivateProfileStringW(L"Input", L"VibrationStrength", L"nan", path.c_str()));
    CHECK(load(loaded, path.wstring(), &error) && loaded.vibration_strength == 80);
    CHECK(WritePrivateProfileStringW(L"Input", L"VibrationStrength", L"45", path.c_str()));
    CHECK(load(loaded, path.wstring(), &error) && loaded.vibration_strength == 45);
    CHECK(loaded.language == c.language && loaded.output == c.output);
    CHECK(loaded.aa_mode == L"off" && std::abs(loaded.sharpness - 1.2f) < 0.001f && loaded.aniso == 4);
    CHECK(loaded.frame_gen == L"fsr3");
    CHECK(loaded.display_mode == L"fullscreen");
    CHECK(WritePrivateProfileStringW(L"Graphics", L"Window", L"windowed", path.c_str()));
    CHECK(load(loaded, path.wstring(), &error) && loaded.display_mode == L"fullscreen");
    CHECK(WritePrivateProfileStringW(L"Graphics", L"display_mode", nullptr, path.c_str()));
    CHECK(load(loaded, path.wstring(), &error) && loaded.display_mode == L"windowed");
    wchar_t ini_frame_gen[16];
    CHECK(GetPrivateProfileStringW(L"Graphics", L"frame_gen", L"", ini_frame_gen, 16, path.c_str()) &&
          std::wstring(ini_frame_gen) == L"fsr3");
    CHECK(WritePrivateProfileStringW(L"Graphics", L"frame_gen", L"off", path.c_str()));
    CHECK(load(loaded, path.wstring(), &error) && loaded.frame_gen == L"off");
    CHECK(WritePrivateProfileStringW(L"Graphics", L"frame_gen", L"invalid", path.c_str()));
    CHECK(std::abs(loaded.deadzone - c.deadzone) < 0.001f && loaded.motion == c.motion);
    CHECK(loaded.keyboard[CROSS].code == 'B' && loaded.gamepad[CROSS].code == SDL_GAMEPAD_BUTTON_WEST);
    CHECK(loaded.device_guid == c.device_guid && loaded.device_serial == c.device_serial);
    CHECK(WritePrivateProfileStringW(L"Graphics", L"Sharpness", L"nan", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Graphics", L"Output", L"nonsense", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Graphics", L"AAMode", L"invalid", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Keyboard", L"14", L"key:9999", path.c_str()));
    CHECK(load(loaded, path.wstring(), &error));
    CHECK(loaded.sharpness == 0.6f && loaded.output == L"720p" && loaded.aa_mode == L"smaa");
    CHECK(loaded.frame_gen == L"off");
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
    SetEnvironmentVariableW(L"PS3_FRAME_GEN", L"off");
    CHECK(resolve_frame_gen(c) == L"off");
    SetEnvironmentVariableW(L"PS3_FRAME_GEN", L"invalid");
    CHECK(resolve_frame_gen(c) == L"fsr3");
    SetEnvironmentVariableW(L"PS3_FRAME_GEN", nullptr);
    CHECK(resolve_frame_gen(c) == L"fsr3");
    SetEnvironmentVariableW(L"PS3_VIBRATION_STRENGTH", nullptr);
    CHECK(resolve_vibration_strength(c) == 45);
    SetEnvironmentVariableW(L"PS3_VIBRATION_STRENGTH", L"0");
    CHECK(resolve_vibration_strength(c) == 0);
    SetEnvironmentVariableW(L"PS3_VIBRATION_STRENGTH", L"50");
    CHECK(resolve_vibration_strength(c) == 50);
    SetEnvironmentVariableW(L"PS3_VIBRATION_STRENGTH", L"100");
    CHECK(resolve_vibration_strength(c) == 100);
    SetEnvironmentVariableW(L"PS3_VIBRATION_STRENGTH", L"150");
    CHECK(resolve_vibration_strength(c) == 100);
    SetEnvironmentVariableW(L"PS3_VIBRATION_STRENGTH", L"-10");
    CHECK(resolve_vibration_strength(c) == 0);
    SetEnvironmentVariableW(L"PS3_VIBRATION_STRENGTH", L"invalid");
    CHECK(resolve_vibration_strength(c) == 45);
    SetEnvironmentVariableW(L"PS3_VIBRATION_STRENGTH", nullptr);
    apply_environment(c, path.wstring());
    wchar_t env[128];
    CHECK(!GetEnvironmentVariableW(L"PAD_AUTOPRESS", env, 128));
    CHECK(!GetEnvironmentVariableW(L"PS3_LANG", env, 128));
    CHECK(GetEnvironmentVariableW(L"PS3_OUTPUT", env, 128) && std::wstring(env) == L"1440p");
    CHECK(GetEnvironmentVariableW(L"PS3_AA", env, 128) && std::wstring(env) == L"off");
    CHECK(GetEnvironmentVariableW(L"PS3_ANISO", env, 128) && std::wstring(env) == L"4");
    CHECK(GetEnvironmentVariableW(L"PS3_FRAME_GEN", env, 128) && std::wstring(env) == L"fsr3");
    CHECK(GetEnvironmentVariableW(L"PS3_FPS_CAP", env, 128) && std::wstring(env) == L"30");
    CHECK(GetEnvironmentVariableW(L"PS3_VIBRATION_STRENGTH", env, 128) && std::wstring(env) == L"45");
    c.frame_gen = L"invalid";
    apply_environment(c, path.wstring());
    CHECK(!GetEnvironmentVariableW(L"PS3_FRAME_GEN", env, 128));
    CHECK(GetEnvironmentVariableW(L"PS3_FPS_CAP", env, 128) && std::wstring(env) == L"30");
    c.frame_gen = L"fsr3";
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
    CHECK(GetEnvironmentVariableW(L"PS3_BUTTON_PROMPTS", env, 128) && std::wstring(env) == L"auto");

    // button_prompts config persistence, env precedence, and controller auto-detection
    {
        CHECK(normalize_button_prompts(L"AUTO") == L"auto");
        CHECK(normalize_button_prompts(L"PlayStation") == L"playstation");
        CHECK(normalize_button_prompts(L"XBOX") == L"xbox");
        CHECK(normalize_button_prompts(L"NiNtEnDo") == L"nintendo");
        CHECK(normalize_button_prompts(L"bogus") == L"auto");
        CHECK(normalize_button_prompts(L"bogus", L"") == L"");

        auto prompt_ini = std::filesystem::temp_directory_path() / L"bakugan-button-prompts-test.ini";
        std::filesystem::remove(prompt_ini);
        for (const wchar_t *mode : {L"auto", L"playstation", L"xbox", L"nintendo"}) {
            Config pc = defaults();
            pc.button_prompts = mode;
            CHECK(save(pc, prompt_ini.wstring(), &error));
            wchar_t ini_prompts[32];
            CHECK(GetPrivateProfileStringW(L"Input", L"button_prompts", L"", ini_prompts, 32, prompt_ini.c_str()) &&
                  std::wstring(ini_prompts) == mode);
            Config plc;
            CHECK(load(plc, prompt_ini.wstring(), &error) && plc.button_prompts == mode);
        }
        CHECK(WritePrivateProfileStringW(L"Input", L"button_prompts", L"XBOX", prompt_ini.c_str()));
        Config pcase;
        CHECK(load(pcase, prompt_ini.wstring(), &error) && pcase.button_prompts == L"xbox");
        CHECK(WritePrivateProfileStringW(L"Input", L"button_prompts", L"invalid_style", prompt_ini.c_str()));
        CHECK(load(pcase, prompt_ini.wstring(), &error) && pcase.button_prompts == L"auto");

        Config penv = defaults();
        penv.button_prompts = L"xbox";
        SetEnvironmentVariableW(L"PS3_BUTTON_PROMPTS", nullptr);
        CHECK(resolve_button_prompts(penv) == L"xbox");
        SetEnvironmentVariableW(L"PS3_BUTTON_PROMPTS", L"Nintendo");
        CHECK(resolve_button_prompts(penv) == L"nintendo");
        SetEnvironmentVariableW(L"PS3_BUTTON_PROMPTS", L"playstation");
        CHECK(resolve_button_prompts(penv) == L"playstation");
        SetEnvironmentVariableW(L"PS3_BUTTON_PROMPTS", L"auto");
        CHECK(resolve_button_prompts(penv) == L"auto");
        SetEnvironmentVariableW(L"PS3_BUTTON_PROMPTS", L"unknown");
        CHECK(resolve_button_prompts(penv) == L"xbox");
        SetEnvironmentVariableW(L"PS3_BUTTON_PROMPTS", nullptr);

        apply_environment(penv, prompt_ini.wstring());
        CHECK(GetEnvironmentVariableW(L"PS3_BUTTON_PROMPTS", env, 128) && std::wstring(env) == L"xbox");
        CHECK(GetEnvironmentVariableW(L"PS3_PLAYER_CONFIG", env, 128));
        penv.button_prompts = L"auto";
        apply_environment(penv, prompt_ini.wstring());
        CHECK(GetEnvironmentVariableW(L"PS3_BUTTON_PROMPTS", env, 128) && std::wstring(env) == L"auto");

        CHECK(resolve_prompt_style(L"auto", ButtonPromptStyle::Xbox) == ButtonPromptStyle::Xbox);
        CHECK(resolve_prompt_style(L"auto", ButtonPromptStyle::Nintendo) == ButtonPromptStyle::Nintendo);
        CHECK(resolve_prompt_style(L"auto", ButtonPromptStyle::PlayStation) == ButtonPromptStyle::PlayStation);
        CHECK(resolve_prompt_style(L"xbox", ButtonPromptStyle::Nintendo) == ButtonPromptStyle::Xbox);
        CHECK(resolve_prompt_style(L"nintendo", ButtonPromptStyle::Xbox) == ButtonPromptStyle::Nintendo);
        CHECK(resolve_prompt_style(L"playstation", ButtonPromptStyle::Xbox) == ButtonPromptStyle::PlayStation);

        CHECK(std::string(prompt_style_name(ButtonPromptStyle::PlayStation)) == "playstation");
        CHECK(std::string(prompt_style_name(ButtonPromptStyle::Xbox)) == "xbox");
        CHECK(std::string(prompt_style_name(ButtonPromptStyle::Nintendo)) == "nintendo");

        CHECK(prompt_style_from_gamepad_type(SDL_GAMEPAD_TYPE_XBOX360) == ButtonPromptStyle::Xbox);
        CHECK(prompt_style_from_gamepad_type(SDL_GAMEPAD_TYPE_XBOXONE) == ButtonPromptStyle::Xbox);
        CHECK(prompt_style_from_gamepad_type(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO) == ButtonPromptStyle::Nintendo);
        CHECK(prompt_style_from_gamepad_type(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT) == ButtonPromptStyle::Nintendo);
        CHECK(prompt_style_from_gamepad_type(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT) == ButtonPromptStyle::Nintendo);
        CHECK(prompt_style_from_gamepad_type(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR) == ButtonPromptStyle::Nintendo);
        CHECK(prompt_style_from_gamepad_type(SDL_GAMEPAD_TYPE_GAMECUBE) == ButtonPromptStyle::Nintendo);
        CHECK(prompt_style_from_gamepad_type(SDL_GAMEPAD_TYPE_PS3) == ButtonPromptStyle::PlayStation);
        CHECK(prompt_style_from_gamepad_type(SDL_GAMEPAD_TYPE_PS4) == ButtonPromptStyle::PlayStation);
        CHECK(prompt_style_from_gamepad_type(SDL_GAMEPAD_TYPE_PS5) == ButtonPromptStyle::PlayStation);
        CHECK(prompt_style_from_gamepad_type(SDL_GAMEPAD_TYPE_STANDARD) == ButtonPromptStyle::PlayStation);
        CHECK(prompt_style_from_gamepad_type(SDL_GAMEPAD_TYPE_UNKNOWN) == ButtonPromptStyle::PlayStation);

        std::filesystem::remove(prompt_ini);
        SetEnvironmentVariableW(L"PS3_BUTTON_PROMPTS", nullptr);
    }
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
    CHECK(r.sensors[0] == 399 && r.sensors[1] == 739 && r.sensors[2] == 625 && r.sensors[3] == 389);
    std::array<float, ACTION_COUNT> v_sym{};
    v_sym[R_RIGHT] = 1.0f;
    v_sym[R_UP] = 1.0f;
    r = make_report(v_sym, MotionMode::RightStick, false, a, g, 1000, pulse);
    CHECK(r.sensors[0] == 625 && r.sensors[1] == 59 && r.sensors[2] == 399 && r.sensors[3] == 635);
    std::array<float, ACTION_COUNT> v_horiz{};
    v_horiz[R_RIGHT] = 1.0f;
    r = make_report(v_horiz, MotionMode::RightStick, false, a, g, 1000, pulse);
    CHECK(r.sensors[0] == 625 && r.sensors[1] == 399 && r.sensors[2] == 512 && r.sensors[3] == 635);
    std::array<float, ACTION_COUNT> v_half_pos{}, v_half_neg{};
    v_half_pos[R_RIGHT] = 0.5f;
    v_half_pos[R_UP] = 0.5f;
    v_half_neg[R_LEFT] = 0.5f;
    v_half_neg[R_DOWN] = 0.5f;
    auto r_pos = make_report(v_half_pos, MotionMode::RightStick, false, a, g, 1000, pulse);
    auto r_neg = make_report(v_half_neg, MotionMode::RightStick, false, a, g, 1000, pulse);
    CHECK(r_pos.sensors[0] == 569 && r_neg.sensors[0] == 455);
    CHECK(r_pos.sensors[2] == 455 && r_neg.sensors[2] == 569);
    CHECK(r_pos.sensors[1] == 229 && r_neg.sensors[1] == 569);
    v[MOTION_THROW] = 1;
    const auto pulse0 = make_report(v, MotionMode::RightStick, false, a, g, 1000, pulse);
    const auto pulse1 = make_report(v, MotionMode::RightStick, false, a, g, 1100, pulse);
    const auto pulse2 = make_report(v, MotionMode::RightStick, false, a, g, 1150, pulse);
    const auto pulse3 = make_report(v, MotionMode::RightStick, false, a, g, 1200, pulse);
    const auto pulse4 = make_report(v, MotionMode::RightStick, false, a, g, 1400, pulse);
    CHECK(pulse0.sensors[1] == 59 && pulse0.sensors[2] == 1023);
    CHECK(pulse1.sensors[1] == 229 && pulse1.sensors[2] == 0);
    CHECK(pulse2.sensors[1] == 314 && pulse2.sensors[2] == 0);
    CHECK(pulse3.sensors[1] == 739 && pulse3.sensors[2] == 625);
    CHECK(pulse4.sensors[1] == 739 && pulse4.sensors[2] == 625);
    v[MOTION_THROW] = 0;
    make_report(v, MotionMode::RightStick, false, a, g, 1500, pulse);
    v[MOTION_THROW] = 1;
    const auto pulse_re = make_report(v, MotionMode::RightStick, false, a, g, 1600, pulse);
    CHECK(pulse_re.sensors[1] == 59 && pulse_re.sensors[2] == 1023);
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

    // The left stick keeps the same speed for axial and diagonal travel.
    constexpr float aim_deadzone = 0.10f;
    shape_aim_stick(0.08f, 0.04f, aim_deadzone, sx, sy);
    CHECK(sx == 0.f && sy == 0.f);
    shape_aim_stick(0.4f, 0.f, aim_deadzone, sx, sy);
    const float aim_speed = sx;
    CHECK(aim_speed > 0.20f && aim_speed < 0.30f && sy == 0.f);
    shape_aim_stick(0.4f / std::sqrt(2.f), 0.4f / std::sqrt(2.f), aim_deadzone, sx, sy);
    CHECK(std::abs(std::hypot(sx, sy) - aim_speed) < 1e-5f);
    shape_aim_stick(0.85f, 0.f, aim_deadzone, sx, sy);
    CHECK(sx < 0.85f);
    shape_aim_stick(1.f, 0.f, aim_deadzone, sx, sy);
    CHECK(sx == 1.f && sy == 0.f);

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

    // Fall back to Fast for missing settings while preserving explicit profile and tuning keys.
    auto legacy_path = std::filesystem::temp_directory_path() / L"bakugan-legacy-test.ini";
    std::filesystem::remove(legacy_path);
    CHECK(WritePrivateProfileStringW(L"Player", L"Version", L"1", legacy_path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Input", L"Deadzone", L"0.22", legacy_path.c_str()));
    Config legacy_cfg;
    CHECK(load(legacy_cfg, legacy_path.wstring(), &error));
    CHECK(legacy_cfg.sixaxis_profile == SixaxisProfile::Fast);
    CHECK(legacy_cfg.vibration_strength == 80);
    CHECK(legacy_cfg.button_prompts == L"auto");
    CHECK(legacy_cfg.frame_gen == L"off");
    CHECK(legacy_cfg.display_mode == L"borderless");
    CHECK(std::abs(legacy_cfg.deadzone - 0.22f) < 0.001f);
    CHECK(std::abs(legacy_cfg.sixaxis_smoothing - 0.15f) < 0.001f);
    CHECK(WritePrivateProfileStringW(L"Input", L"SixaxisProfile", L"1", legacy_path.c_str()));
    CHECK(load(legacy_cfg, legacy_path.wstring(), &error));
    CHECK(legacy_cfg.sixaxis_profile == SixaxisProfile::Standard);
    CHECK(legacy_cfg.sixaxis_smoothing == 0.45f && legacy_cfg.deadzone == 0.22f);
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
    apply_sixaxis_profile(mode_cfg, SixaxisProfile::Standard);
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
    CHECK(stx > 0.90f); // physical yaw drives tilt_x (+right)
    SixaxisState roll_state;
    float rtx = 0.f, rty = 0.f;
    for (int i = 0; i < 20; ++i)
        step_sixaxis(roll_state, 0.0f, 0.0f, 0.0f, true,
                     std::array<float, 3>{0.f, 9.80665f, 0.f},
                     std::array<float, 3>{0.f, 0.f, -2.0f}, mode_cfg, 0.020f, rtx, rty, out_a, out_g);
    CHECK(rtx > 0.90f); // steering-wheel roll (g[2]) also drives tilt_x (+right)
    auto roll_rep = make_report_with_tilt(std::array<float, ACTION_COUNT>{}, rtx, rty,
                                          MotionMode::Sensors, true, out_a, out_g, 2500, pulse);
    CHECK(roll_rep.sensors[0] == 512 && roll_rep.sensors[3] > 512); // stick_s0 is not added when has_real is true

    // Gravity-referenced roll holds steady tilt angle even after gyro rate drops to 0,
    // without duplicating stick_s0 on top of base_s0
    SixaxisState hold_state;
    float htx = 0.f, hty = 0.f;
    const float tilt25 = 25.0f * (3.14159265f / 180.0f);
    std::array<float, 3> held_accel{-9.80665f * std::sin(tilt25), 9.80665f * std::cos(tilt25), 0.0f};
    for (int i = 0; i < 30; ++i)
        step_sixaxis(hold_state, 0.0f, 0.0f, 0.0f, true, held_accel,
                     std::array<float, 3>{0.f, 0.f, 0.f}, mode_cfg, 0.010f, htx, hty, out_a, out_g);
    CHECK(htx > 0.70f); // holding 25 deg right roll maintains steady steering
    auto hold_rep = make_report_with_tilt(std::array<float, ACTION_COUNT>{}, htx, hty,
                                          MotionMode::Sensors, true, out_a, out_g, 2600, pulse);
    CHECK(hold_rep.sensors[0] == 560 && hold_rep.sensors[3] == 512);

    // gyro_invert_x also inverts base_s0 (SENSOR_X) around 512
    Config inv_cfg = mode_cfg;
    inv_cfg.gyro_invert_x = true;
    SixaxisState inv_state;
    float itx = 0.f, ity = 0.f;
    for (int i = 0; i < 30; ++i)
        step_sixaxis(inv_state, 0.0f, 0.0f, 0.0f, true, held_accel,
                     std::array<float, 3>{0.f, 0.f, -1.0f}, inv_cfg, 0.010f, itx, ity, out_a, out_g);
    auto inv_rep = make_report_with_tilt(std::array<float, ACTION_COUNT>{}, itx, ity,
                                         MotionMode::Sensors, true, out_a, out_g, 2700, pulse);
    CHECK(itx < -0.70f && inv_rep.sensors[0] == 464 && inv_rep.sensors[3] < 512);

    // Sub-deadzone raw_gyro on roll/pitch must not integrate into complementary filter
    SixaxisState dz_cf_state;
    float dztx = 0.f, dzty = 0.f;
    for (int i = 0; i < 50; ++i)
        step_sixaxis(dz_cf_state, 0.0f, 0.0f, 0.0f, true,
                     std::array<float, 3>{0.f, 9.80665f, 0.f},
                     std::array<float, 3>{0.04f, 0.0f, -0.04f}, mode_cfg, 0.020f, dztx, dzty, out_a, out_g);
    CHECK(dztx == 0.0f && dzty == 0.0f && dz_cf_state.roll_angle == 0.0f && dz_cf_state.pitch_angle == 0.0f);

    // Ergonomic tilted hand posture calibrates neutral pitch, then further forward tilt drives up (-y)
    SixaxisState pose_state;
    float ptx = 0.f, pty = 0.f;
    const float rest20 = 20.0f * (3.14159265f / 180.0f);
    std::array<float, 3> rest_accel{0.0f, 9.80665f * std::cos(rest20), 9.80665f * std::sin(rest20)};
    for (int i = 0; i < 40; ++i)
        step_sixaxis(pose_state, 0.0f, 0.0f, 0.0f, true, rest_accel,
                     std::array<float, 3>{0.f, 0.f, 0.f}, mode_cfg, 0.025f, ptx, pty, out_a, out_g);
    CHECK(std::abs(pty) < 0.05f);
    const float fwd35 = 35.0f * (3.14159265f / 180.0f);
    std::array<float, 3> fwd_accel{0.0f, 9.80665f * std::cos(fwd35), 9.80665f * std::sin(fwd35)};
    for (int i = 0; i < 40; ++i)
        step_sixaxis(pose_state, 0.0f, 0.0f, 0.0f, true, fwd_accel,
                     std::array<float, 3>{-0.5f, 0.f, 0.f}, mode_cfg, 0.010f, ptx, pty, out_a, out_g);
    CHECK(pty < -0.45f);

    mode_cfg.motion = MotionMode::RightStick;
    SixaxisState stick_only_state;
    for (int i = 0; i < 20; ++i)
        step_sixaxis(stick_only_state, 0.0f, 0.0f, 0.0f, true,
                     std::array<float, 3>{0.f, 9.80665f, 0.f},
                     std::array<float, 3>{0.f, -2.0f, 0.f}, mode_cfg, 0.020f, stx, sty, out_a, out_g);
    CHECK(stx == 0.0f && sty == 0.0f); // RightStick mode ignores physical gyro

    // Test the complete report used by the SDL bridge with both sources active.
    Config hybrid = defaults();
    const std::array<float, 3> turn_right{0.f, -2.f, 0.f};
    const auto steady_report = [&](const Config &cfg, const std::array<float, ACTION_COUNT> &input) {
        SixaxisState state;
        MotionPulse held;
        Report report;
        for (unsigned i = 0; i < 80; ++i)
            report = make_gamepad_report(input, cfg, true, held_accel, turn_right,
                                         0.004f, 3000 + i * 4, state, held);
        return report;
    };
    std::array<float, ACTION_COUNT> hybrid_stick{};
    const auto gyro_alone = steady_report(hybrid, hybrid_stick);
    CHECK(gyro_alone.rx > 200 && gyro_alone.sensors[0] > 512 && gyro_alone.sensors[3] > 512);

    // Stick R inside deadzone must hand over cleanly to the gyroscope alone.
    float dz_rx = 0.0f, dz_ry = 0.0f;
    shape_stick_2d(0.05f, -0.05f, hybrid.deadzone, dz_rx, dz_ry);
    std::array<float, ACTION_COUNT> deadzone_stick{};
    deadzone_stick[R_RIGHT] = std::max(0.0f, dz_rx);
    deadzone_stick[R_UP] = std::max(0.0f, -dz_ry);
    const auto gyro_deadzone = steady_report(hybrid, deadzone_stick);
    CHECK(gyro_deadzone.rx == gyro_alone.rx && gyro_deadzone.ry == gyro_alone.ry &&
          gyro_deadzone.sensors == gyro_alone.sensors);

    hybrid_stick[R_UP] = 0.65f;
    const auto combined = steady_report(hybrid, hybrid_stick);
    CHECK(combined.rx > 200 && combined.ry < 70);
    CHECK(combined.sensors[0] == gyro_alone.sensors[0] && combined.sensors[3] == gyro_alone.sensors[3]);
    CHECK(combined.sensors[1] < gyro_alone.sensors[1] && combined.sensors[2] < gyro_alone.sensors[2]);

    hybrid_stick[R_LEFT] = 1.0f;
    const auto opposite = steady_report(hybrid, hybrid_stick);
    CHECK(opposite.rx == 0 && opposite.ry < 70);
    CHECK(opposite.sensors[0] < 512 && opposite.sensors[0] > 350);
    CHECK(opposite.sensors[1] < gyro_alone.sensors[1] && opposite.sensors[2] < gyro_alone.sensors[2]);

    std::array<float, ACTION_COUNT> tight_stick{};
    tight_stick[R_RIGHT] = 1.0f;
    const auto tight = steady_report(hybrid, tight_stick);
    const int expected_tight_s0 =
        static_cast<int>(std::clamp(std::lround(gyro_alone.sensors[0] + 113.0f * hybrid.gyro_sensitivity), 0l, 1023l));
    CHECK(tight.sensors[0] > gyro_alone.sensors[0] && tight.sensors[0] <= 1023);
    CHECK(std::abs((int)tight.sensors[0] - expected_tight_s0) <= 1);
    CHECK(tight.sensors[1] == gyro_alone.sensors[1] && tight.sensors[2] == gyro_alone.sensors[2]);

    hybrid.motion = MotionMode::Sensors;
    const auto only_sensors = steady_report(hybrid, hybrid_stick);
    CHECK(only_sensors.rx == gyro_alone.rx && only_sensors.ry == gyro_alone.ry &&
          only_sensors.sensors == gyro_alone.sensors);

    hybrid.motion = MotionMode::RightStick;
    const auto only_stick = steady_report(hybrid, hybrid_stick);
    const int expected_stick_left =
        static_cast<int>(512l - std::lround(113.0f * hybrid.gyro_sensitivity));
    CHECK(only_stick.rx == 0 && only_stick.ry < 70 &&
          std::abs((int)only_stick.sensors[0] - expected_stick_left) <= 1 &&
          only_stick.sensors[1] < 399 && only_stick.sensors[2] < 512 && only_stick.sensors[3] < 512);
    const auto only_stick_horiz = steady_report(hybrid, tight_stick);
    CHECK(only_stick_horiz.sensors[1] == 399 && only_stick_horiz.sensors[2] == 512);

    // Sixaxis profiles scale Stick R travel proportionally over the calibrated +-113 range.
    std::array<float, ACTION_COUNT> full_ru{};
    full_ru[R_RIGHT] = 1.0f;
    full_ru[R_UP] = 1.0f;
    const auto profile_report = [&](SixaxisProfile prof) {
        Config pcfg = defaults();
        apply_sixaxis_profile(pcfg, prof);
        pcfg.motion = MotionMode::RightStick;
        SixaxisState pst;
        MotionPulse pp;
        Report rep;
        for (unsigned i = 0; i < 120; ++i)
            rep = make_gamepad_report(full_ru, pcfg, false, fa, std::array<float, 3>{},
                                      0.010f, 5000 + i * 10, pst, pp);
        return rep;
    };
    const auto prof_smooth = profile_report(SixaxisProfile::Smooth);
    const auto prof_standard = profile_report(SixaxisProfile::Standard);
    const auto prof_fast = profile_report(SixaxisProfile::Fast);
    CHECK(prof_smooth.sensors[0] == 608 && prof_smooth.sensors[2] == 416);
    CHECK(prof_standard.sensors[0] == 625 && prof_standard.sensors[2] == 399);
    CHECK(prof_fast.sensors[0] == 670 && prof_fast.sensors[2] == 354);
    CHECK(prof_smooth.sensors[0] < prof_standard.sensors[0] && prof_standard.sensors[0] < prof_fast.sensors[0]);

    // Vibration scaling (0%, 50%, 100%, small motor balance, and out-of-range clamping)
    uint16_t r_lo = 0, r_hi = 0;
    scale_rumble_motors(65535, 65535, 0, r_lo, r_hi);
    CHECK(r_lo == 0 && r_hi == 0);
    scale_rumble_motors(65535, 65535, 50, r_lo, r_hi);
    CHECK(r_lo == 32768 && r_hi == 22937 && r_hi < r_lo);
    scale_rumble_motors(65535, 65535, 100, r_lo, r_hi);
    CHECK(r_lo == 65535 && r_hi == 45875 && r_hi < r_lo);
    scale_rumble_motors(128 * 257, 0, 50, r_lo, r_hi);
    CHECK(r_lo == 16448 && r_hi == 0);
    scale_rumble_motors(65535, 65535, -20, r_lo, r_hi);
    CHECK(r_lo == 0 && r_hi == 0);
    scale_rumble_motors(65535, 65535, 200, r_lo, r_hi);
    CHECK(r_lo == 65535 && r_hi == 45875);

    // Mouse leaky virtual stick: captures 1px sweeps, responds symmetrically in X/Y on the calibrated
    // +-113 range without jump-spike workarounds, counter-steers crisply, and returns smoothly to 0
    float mvx = 0.f, mvy = 0.f;
    step_mouse_virtual_stick(mvx, mvy, 0, 0, 0.5f, 0.008f);
    CHECK(mvx == 0.0f && mvy == 0.0f);
    step_mouse_virtual_stick(mvx, mvy, 1, 0, 0.5f, 0.008f);
    CHECK(mvx > 0.0f && mvx < 0.02f && mvy == 0.0f);
    float dummy_x = 0.f, mvy_fwd = 0.f, mvy_back = 0.f;
    step_mouse_virtual_stick(dummy_x, mvy_fwd, 0, -6, 0.5f, 0.004f);
    step_mouse_virtual_stick(dummy_x, mvy_back, 0, 6, 0.5f, 0.004f);
    CHECK(mvy_fwd < 0.0f && std::abs(mvy_fwd + mvy_back) < 1e-6f);
    mvy_fwd = 0.f;
    mvy_back = 0.f;
    step_mouse_virtual_stick(dummy_x, mvy_fwd, 0, -200, 0.5f, 0.008f);
    step_mouse_virtual_stick(dummy_x, mvy_back, 0, 200, 0.5f, 0.008f);
    CHECK(mvy_fwd == -1.0f && mvy_back == 1.0f);
    mvx = 0.f;
    mvy = 0.f;
    for (int i = 0; i < 15; ++i)
        step_mouse_virtual_stick(mvx, mvy, 12, 0, 0.5f, 0.008f);
    CHECK(mvx > 0.90f && mvx <= 1.0f);
    float peak_x = mvx;
    step_mouse_virtual_stick(mvx, mvy, -12, 0, 0.5f, 0.008f); // counter-steer cuts turn faster
    CHECK(mvx < peak_x - 0.20f);
    float cmvy = -1.0f;
    step_mouse_virtual_stick(dummy_x, cmvy, 0, 12, 0.5f, 0.008f); // counter-steer from forward boost to brake
    CHECK(cmvy > -1.0f + 0.20f);
    for (int i = 0; i < 160; ++i)
        step_mouse_virtual_stick(mvx, mvy, 0, 0, 0.5f, 0.010f);
    CHECK(mvx == 0.0f && mvy == 0.0f);

    // Exact PS3 Sixaxis axis decoder (func_002E3930 / func_002E3374: deadzone 0.035, clamp 3.0G)
    const auto decode_ps3_axis = [](uint16_t s) {
        const float raw = static_cast<float>(static_cast<int>(s) - 512) / 113.0f;
        const float mag = std::min(std::abs(raw), 3.0f);
        if (mag <= 0.035f)
            return 0.0f;
        return std::copysign((mag - 0.035f) / 0.965f, raw);
    };

    // Mouse Bakugan throw (CShootControlBakuganTask::func_000E4804): a 1-frame casual forward flick (-80 px)
    // crosses Y < -1.0 and delta_Y < -1.0 across Smooth, Standard, and Fast profiles
    for (SixaxisProfile prof : {SixaxisProfile::Smooth, SixaxisProfile::Standard, SixaxisProfile::Fast}) {
        Config tcfg = defaults();
        apply_sixaxis_profile(tcfg, prof);
        SixaxisState tst;
        MotionPulse tp;
        float vx = 0.f, vy = 0.f;
        const float prev_y = decode_ps3_axis(399);
        Report throw_rep;
        for (int i = 0; i < 8; ++i) {
            step_mouse_virtual_stick(vx, vy, 0, -10, tcfg.mouse_sensitivity, 0.004f);
            throw_rep = make_keyboard_mouse_report(std::array<float, ACTION_COUNT>{}, vx, vy, tcfg, 0.004f,
                                                   6000 + static_cast<uint64_t>(i) * 4, tst, tp);
        }
        const float curr_y = decode_ps3_axis(throw_rep.sensors[1]);
        CHECK(curr_y < -1.0f && (curr_y - prev_y) < -1.0f);
    }

    // End-to-end mouse Bakugan steering & throw: calibrated +-113 accel/brake on sensors[2],
    // sensors[1] throw/shake range (59..739) without jump spikes on release/reversal, and horizontal isolation
    const auto run_mouse_sweep = [&](SixaxisProfile prof, MotionMode mode, int dx, int dy, int first_dx, int first_dy) {
        Config mcfg = defaults();
        apply_sixaxis_profile(mcfg, prof);
        mcfg.motion = mode;
        SixaxisState mst;
        MotionPulse mp;
        float vx = 0.f, vy = 0.f;
        Report rep;
        for (int i = 0; i < 80; ++i) {
            step_mouse_virtual_stick(vx, vy, i == 0 ? first_dx : dx, i == 0 ? first_dy : dy,
                                     mcfg.mouse_sensitivity, 0.008f);
            rep = make_keyboard_mouse_report(std::array<float, ACTION_COUNT>{}, vx, vy, mcfg, 0.008f,
                                             7000 + static_cast<uint64_t>(i) * 8, mst, mp);
        }
        return rep;
    };
    const auto m_fwd_smooth = run_mouse_sweep(SixaxisProfile::Smooth, MotionMode::Auto, 0, -20, 0, -200);
    const auto m_fwd_standard = run_mouse_sweep(SixaxisProfile::Standard, MotionMode::Auto, 0, -20, 0, -200);
    const auto m_fwd_fast = run_mouse_sweep(SixaxisProfile::Fast, MotionMode::Auto, 0, -20, 0, -200);
    CHECK(m_fwd_smooth.sensors[2] == 416 && m_fwd_standard.sensors[2] == 399 && m_fwd_fast.sensors[2] == 354);
    CHECK(m_fwd_smooth.sensors[1] == 59 && m_fwd_standard.sensors[1] == 59 && m_fwd_fast.sensors[1] == 59);
    CHECK(m_fwd_fast.ry == 0 && m_fwd_standard.ry == 0 && m_fwd_smooth.ry == 0);

    const auto m_brk_smooth = run_mouse_sweep(SixaxisProfile::Smooth, MotionMode::Auto, 0, 20, 0, 200);
    const auto m_brk_standard = run_mouse_sweep(SixaxisProfile::Standard, MotionMode::Auto, 0, 20, 0, 200);
    const auto m_brk_fast = run_mouse_sweep(SixaxisProfile::Fast, MotionMode::Auto, 0, 20, 0, 200);
    CHECK(m_brk_smooth.sensors[2] == 608 && m_brk_standard.sensors[2] == 625 && m_brk_fast.sensors[2] == 670);
    CHECK(m_brk_smooth.sensors[1] == 739 && m_brk_standard.sensors[1] == 739 && m_brk_fast.sensors[1] == 739);
    CHECK(m_brk_fast.ry == 255 && m_brk_standard.ry == 255 && m_brk_smooth.ry == 255);
    CHECK((512 - m_fwd_fast.sensors[2]) == (m_brk_fast.sensors[2] - 512));

    const auto m_horiz = run_mouse_sweep(SixaxisProfile::Fast, MotionMode::Auto, 20, 0, 20, 0);
    CHECK(m_horiz.sensors[0] == 670 && m_horiz.sensors[1] == 399 &&
          m_horiz.sensors[2] == 512 && m_horiz.sensors[3] == 684);

    // Rolling jump safety (func_000C6F90): bounded upward slew keeps delta_Y <= +1.0G/frame (< +1.10G)
    // across sudden forward->idle release, forward->brake mouse reversal, and Stick R forward->brake snap
    {
        Config scfg = defaults();
        auto check_mouse_recovery = [&](int rev_first_dy, int rev_dy, uint16_t expected_end_s1) {
            SixaxisState mst;
            MotionPulse mp;
            float vx = 0.f, vy = 0.f;
            Report rep;
            for (int i = 0; i < 80; ++i) {
                step_mouse_virtual_stick(vx, vy, 0, -20, scfg.mouse_sensitivity, 0.008f);
                rep = make_keyboard_mouse_report(std::array<float, ACTION_COUNT>{}, vx, vy, scfg, 0.008f,
                                                 11000 + static_cast<uint64_t>(i) * 8, mst, mp);
            }
            CHECK(rep.sensors[1] == 59);
            float prev_y = decode_ps3_axis(rep.sensors[1]);
            uint64_t t_ms = 12000;
            for (int f = 0; f < 40; ++f) {
                for (int sub = 0; sub < 4; ++sub) {
                    const int dy = (f == 0 && sub == 0) ? rev_first_dy : rev_dy;
                    step_mouse_virtual_stick(vx, vy, 0, dy, scfg.mouse_sensitivity, 0.00825f);
                    t_ms += 8;
                    rep = make_keyboard_mouse_report(std::array<float, ACTION_COUNT>{}, vx, vy, scfg, 0.00825f,
                                                     t_ms, mst, mp);
                }
                const float curr_y = decode_ps3_axis(rep.sensors[1]);
                CHECK((curr_y - prev_y) <= 1.0f);
                prev_y = curr_y;
            }
            CHECK(rep.sensors[1] == expected_end_s1);
        };
        check_mouse_recovery(0, 0, 399);
        check_mouse_recovery(200, 20, 739);

        Config rcfg = defaults();
        rcfg.motion = MotionMode::RightStick;
        SixaxisState pst;
        MotionPulse pp;
        std::array<float, ACTION_COUNT> stick_fwd{};
        stick_fwd[R_UP] = 1.0f;
        Report srep;
        for (int i = 0; i < 40; ++i)
            srep = make_gamepad_report(stick_fwd, rcfg, false, fa, std::array<float, 3>{},
                                       0.008f, 13000 + static_cast<uint64_t>(i) * 8, pst, pp);
        CHECK(srep.sensors[1] == 59);
        std::array<float, ACTION_COUNT> stick_brk{};
        stick_brk[R_DOWN] = 1.0f;
        float prev_sy = decode_ps3_axis(srep.sensors[1]);
        uint64_t ts_ms = 14000;
        for (int f = 0; f < 20; ++f) {
            for (int sub = 0; sub < 4; ++sub) {
                ts_ms += 8;
                srep = make_gamepad_report(stick_brk, rcfg, false, fa, std::array<float, 3>{},
                                           0.00825f, ts_ms, pst, pp);
            }
            const float curr_sy = decode_ps3_axis(srep.sensors[1]);
            CHECK((curr_sy - prev_sy) <= 1.0f);
            prev_sy = curr_sy;
        }
        CHECK(srep.sensors[1] == 739);
    }

    // Diagonal turn + boost under MotionMode::Sensors still steers cleanly and decays to rest
    {
        Config mcfg = defaults();
        mcfg.motion = MotionMode::Sensors;
        SixaxisState mst;
        MotionPulse mp;
        float vx = 0.f, vy = 0.f;
        Report diag_rep;
        for (int i = 0; i < 80; ++i) {
            step_mouse_virtual_stick(vx, vy, 20, -20, mcfg.mouse_sensitivity, 0.008f);
            diag_rep = make_keyboard_mouse_report(std::array<float, ACTION_COUNT>{}, vx, vy, mcfg, 0.008f,
                                                  8000 + static_cast<uint64_t>(i) * 8, mst, mp);
            for (int s = 0; s < 4; ++s)
                CHECK(diag_rep.sensors[s] <= 1023);
        }
        CHECK(diag_rep.rx == 255 && diag_rep.ry == 0);
        CHECK(diag_rep.sensors[0] == 670 && diag_rep.sensors[1] == 59 &&
              diag_rep.sensors[2] == 354 && diag_rep.sensors[3] == 684);

        for (int i = 0; i < 160; ++i) {
            step_mouse_virtual_stick(vx, vy, 0, 0, mcfg.mouse_sensitivity, 0.010f);
            diag_rep = make_keyboard_mouse_report(std::array<float, ACTION_COUNT>{}, vx, vy, mcfg, 0.010f,
                                                  9000 + static_cast<uint64_t>(i) * 10, mst, mp);
        }
        CHECK(vx == 0.0f && vy == 0.0f);
        CHECK(diag_rep.rx == 128 && diag_rep.ry == 128);
        CHECK(diag_rep.sensors[0] == 512 && diag_rep.sensors[1] == 399 &&
              diag_rep.sensors[2] == 512 && diag_rep.sensors[3] == 512);

        // Counter-acting a held keyboard direction returns rx and sensors[0] to neutral
        std::array<float, ACTION_COUNT> kb_right{};
        kb_right[R_RIGHT] = 1.0f;
        SixaxisState kst;
        MotionPulse kp;
        Report oppose_rep;
        for (int i = 0; i < 80; ++i)
            oppose_rep = make_keyboard_mouse_report(kb_right, -1.0f, 0.0f, mcfg, 0.008f,
                                                    10000 + static_cast<uint64_t>(i) * 8, kst, kp);
        CHECK(oppose_rep.rx == 128 && oppose_rep.sensors[0] == 512);
    }
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

        // FSR 3.1 transitions keep phase when base_cap stays 30, and re-anchor only when base_cap changes
        double next_trans = 11.98;
        CHECK(!bakugan::fps_cap_reanchor_on_transition(30, 30, 12.0, &next_trans));
        CHECK(std::abs(next_trans - 11.98) < 1e-9);
        CHECK(bakugan::fps_cap_reanchor_on_transition(60, 30, 12.0, &next_trans));
        CHECK(std::abs(next_trans - 12.0) < 1e-9);
        const double w_after = bakugan::fps_cap_step(12.0, interval, false, &next_trans);
        CHECK(std::abs(w_after - interval) < 1e-9);
        CHECK(std::abs(next_trans - (12.0 + interval)) < 1e-9);
        CHECK(bakugan::fps_cap_reanchor_on_transition(0, 30, 15.0, &next_trans));
        CHECK(std::abs(next_trans - 15.0) < 1e-9);
    }

    // SavedataGdiCache: lazy reuse, bounded capacity, and clean teardown before GdiplusShutdown
    {
        for (int cycle = 0; cycle < 2; ++cycle) {
            Gdiplus::GdiplusStartupInput gdi_in;
            ULONG_PTR gdi_token = 0;
            CHECK(Gdiplus::GdiplusStartup(&gdi_token, &gdi_in, nullptr) == Gdiplus::Ok);
            bakugan::SavedataGdiCache cache;
            auto* fam1 = cache.get_family();
            auto* fam2 = cache.get_family();
            CHECK(fam1 != nullptr && fam1 == fam2);

            auto* f_near = cache.get_format(Gdiplus::StringAlignmentNear, false);
            auto* f_center = cache.get_format(Gdiplus::StringAlignmentCenter, false);
            auto* f_near_wrap = cache.get_format(Gdiplus::StringAlignmentNear, true);
            auto* f_center_wrap = cache.get_format(Gdiplus::StringAlignmentCenter, true);
            CHECK(f_near != nullptr && f_center != nullptr && f_near_wrap != nullptr && f_center_wrap != nullptr);
            CHECK(f_near != f_center && f_near != f_near_wrap && f_center != f_center_wrap);
            CHECK(cache.get_format(Gdiplus::StringAlignmentCenter, true) == f_center_wrap);

            auto* font1 = cache.get_font(22.0f, true);
            auto* font2 = cache.get_font(22.0f, true);
            auto* font3 = cache.get_font(22.0f, false);
            CHECK(font1 != nullptr && font1 == font2 && font1 != font3);

            auto* br1 = cache.get_brush(Gdiplus::Color(255, 214, 178, 74));
            auto* br2 = cache.get_brush(Gdiplus::Color(255, 214, 178, 74));
            auto* br3 = cache.get_brush(Gdiplus::Color(230, 122, 20, 26));
            CHECK(br1 != nullptr && br1 == br2 && br1 != br3);

            for (int i = 0; i < 40; ++i) {
                cache.get_font(12.0f + static_cast<float>(i), (i & 1) != 0);
                cache.get_brush(Gdiplus::Color(255, static_cast<BYTE>(i), 10, 20));
            }
            CHECK(cache.fonts.size() == bakugan::SavedataGdiCache::kMaxFonts);
            CHECK(cache.brushes.size() == bakugan::SavedataGdiCache::kMaxBrushes);

            cache.clear();
            CHECK(cache.family == nullptr);
            CHECK(cache.fonts.empty() && cache.brushes.empty());
            for (auto* fmt : cache.formats) CHECK(fmt == nullptr);
            Gdiplus::GdiplusShutdown(gdi_token);
        }
    }

    std::printf("Player config / input model: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
