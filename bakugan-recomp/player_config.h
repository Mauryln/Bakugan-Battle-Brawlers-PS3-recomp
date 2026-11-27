#pragma once
#include <array>
#include <string>
#include <cstdint>
#include "graphics_config.h"

namespace player {
constexpr int ACTION_COUNT = 25;
enum Action {
    SELECT,
    L3,
    R3,
    START,
    UP,
    RIGHT,
    DOWN,
    LEFT,
    L2,
    R2,
    L1,
    R1,
    TRIANGLE,
    CIRCLE,
    CROSS,
    SQUARE,
    MOTION_THROW,
    L_UP,
    L_RIGHT,
    L_DOWN,
    L_LEFT,
    R_UP,
    R_RIGHT,
    R_DOWN,
    R_LEFT
};
enum class BindingKind { None, Key, Mouse, Button, AxisPositive, AxisNegative };
struct Binding {
    BindingKind kind = BindingKind::None;
    int code = 0;
};
enum class InputMode { Auto, KeyboardMouse, Gamepad };
enum class MotionMode { Auto, Sensors, RightStick };
enum class SixaxisProfile { Smooth, Standard, Fast, Custom };
enum class ButtonPromptStyle : int { PlayStation = 0, Xbox = 1, Nintendo = 2 };
struct Config {
    std::wstring output = L"1080p", display_mode = L"borderless";
    std::wstring fsr = L"auto", aa_mode = L"smaa", language = L"auto", ui_language = L"auto";
    std::wstring frame_gen = bakugan::FRAME_GEN_OFF;
    std::wstring button_prompts = L"auto";
    float sharpness = 0.6f, mouse_sensitivity = 0.5f, deadzone = 0.10f;
    float gyro_sensitivity = 1.40f, gyro_deadzone = 0.03f;
    float sixaxis_curve = 0.15f, sixaxis_smoothing = 0.15f;
    bool gyro_invert_x = false, gyro_invert_y = false;
    int aniso = 16;
    int vibration_strength = 80;
    InputMode input = InputMode::Auto;
    MotionMode motion = MotionMode::Auto;
    SixaxisProfile sixaxis_profile = SixaxisProfile::Fast;
    std::string device_guid, device_serial;
    std::array<Binding, ACTION_COUNT> keyboard{}, gamepad{};
    bool mouse_look = true, mouse_left_cross = true;
};
void apply_sixaxis_profile(Config &config, SixaxisProfile profile);
const wchar_t *action_name(int action, bool english = false);
Config defaults();
bool has_input_overrides(const Config& config);
std::wstring config_path();
bool load(Config &config, const std::wstring &path, std::wstring *error = nullptr);
bool save(const Config &config, const std::wstring &path, std::wstring *error = nullptr);
std::wstring binding_name(Binding binding, bool english = false);
std::wstring binding_text(Binding binding);
bool parse_binding(const std::wstring &text, Binding &binding);
std::wstring resolve_frame_gen(const Config &config);
int resolve_vibration_strength(const Config &config);
std::wstring normalize_button_prompts(const std::wstring &value, const std::wstring &fallback = L"auto");
std::wstring resolve_button_prompts(const Config &config);
ButtonPromptStyle resolve_prompt_style(const std::wstring &setting, ButtonPromptStyle detected);
const char *prompt_style_name(ButtonPromptStyle style);
void apply_environment(const Config &config, const std::wstring &path);
} // namespace player
