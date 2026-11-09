#pragma once
#include <array>
#include <string>
#include <cstdint>

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
struct Config {
    std::wstring output = L"1080p", cap = L"30", window = L"borderless";
    std::wstring fsr = L"auto", language = L"auto", ui_language = L"auto";
    float sharpness = 0.6f, mouse_sensitivity = 0.5f, deadzone = 0.15f;
    int aniso = 16;
    InputMode input = InputMode::Auto;
    MotionMode motion = MotionMode::Auto;
    std::string device_guid, device_serial;
    std::array<Binding, ACTION_COUNT> keyboard{}, gamepad{};
    bool mouse_look = true, mouse_left_cross = true;
};
const wchar_t *action_name(int action, bool english = false);
Config defaults();
bool has_input_overrides(const Config& config);
std::wstring config_path();
bool load(Config &config, const std::wstring &path, std::wstring *error = nullptr);
bool save(const Config &config, const std::wstring &path, std::wstring *error = nullptr);
std::wstring binding_name(Binding binding, bool english = false);
std::wstring binding_text(Binding binding);
bool parse_binding(const std::wstring &text, Binding &binding);
void apply_environment(const Config &config, const std::wstring &path);
} // namespace player
