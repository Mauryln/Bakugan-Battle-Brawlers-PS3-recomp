#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <SDL3/SDL.h>
#include "player_config.h"
#include "graphics_config.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cwchar>
#include <cmath>
#include <cstring>

namespace player {
const wchar_t *action_name(int a, bool english) {
    static const wchar_t *names[ACTION_COUNT] = {L"Select",
                                                 L"L3",
                                                 L"R3",
                                                 L"Start",
                                                 L"D-pad arriba",
                                                 L"D-pad derecha",
                                                 L"D-pad abajo",
                                                 L"D-pad izquierda",
                                                 L"L2",
                                                 L"R2",
                                                 L"L1",
                                                 L"R1",
                                                 L"Triángulo",
                                                 L"Círculo",
                                                 L"Cruz",
                                                 L"Cuadrado",
                                                 L"Impulso Sixaxis",
                                                 L"Stick L arriba",
                                                 L"Stick L derecha",
                                                 L"Stick L abajo",
                                                 L"Stick L izquierda",
                                                 L"Stick R arriba",
                                                 L"Stick R derecha",
                                                 L"Stick R abajo",
                                                 L"Stick R izquierda"};
    static const wchar_t *names_en[ACTION_COUNT] = {L"Select",
                                                    L"L3",
                                                    L"R3",
                                                    L"Start",
                                                    L"D-pad up",
                                                    L"D-pad right",
                                                    L"D-pad down",
                                                    L"D-pad left",
                                                    L"L2",
                                                    L"R2",
                                                    L"L1",
                                                    L"R1",
                                                    L"Triangle",
                                                    L"Circle",
                                                    L"Cross",
                                                    L"Square",
                                                    L"Sixaxis throw",
                                                    L"Left stick up",
                                                    L"Left stick right",
                                                    L"Left stick down",
                                                    L"Left stick left",
                                                    L"Right stick up",
                                                    L"Right stick right",
                                                    L"Right stick down",
                                                    L"Right stick left"};
    return a >= 0 && a < ACTION_COUNT ? (english ? names_en : names)[a] : L"";
}
Config defaults() {
    Config c;
    const int keys[ACTION_COUNT] = {
        VK_TAB, '3', 'R', VK_RETURN, VK_UP, VK_RIGHT, VK_DOWN, VK_LEFT, '1', '2', 'Q', 'E', 'S',
        'C',    'X', 'Z', 'F',       VK_UP, VK_RIGHT, VK_DOWN, VK_LEFT, 'I', 'L', 'K', 'J'};
    const int buttons[17] = {SDL_GAMEPAD_BUTTON_BACK,
                             SDL_GAMEPAD_BUTTON_LEFT_STICK,
                             SDL_GAMEPAD_BUTTON_RIGHT_STICK,
                             SDL_GAMEPAD_BUTTON_START,
                             SDL_GAMEPAD_BUTTON_DPAD_UP,
                             SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
                             SDL_GAMEPAD_BUTTON_DPAD_DOWN,
                             SDL_GAMEPAD_BUTTON_DPAD_LEFT,
                             -1,
                             -1,
                             SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
                             SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
                             SDL_GAMEPAD_BUTTON_NORTH,
                             SDL_GAMEPAD_BUTTON_EAST,
                             SDL_GAMEPAD_BUTTON_SOUTH,
                             SDL_GAMEPAD_BUTTON_WEST,
                             SDL_GAMEPAD_BUTTON_RIGHT_STICK};
    for (int a = 0; a < ACTION_COUNT; ++a)
        c.keyboard[a] = {BindingKind::Key, keys[a]};
    for (int a = 0; a < 17; ++a)
        if (buttons[a] >= 0)
            c.gamepad[a] = {BindingKind::Button, buttons[a]};
    c.gamepad[L2] = {BindingKind::AxisPositive, SDL_GAMEPAD_AXIS_LEFT_TRIGGER};
    c.gamepad[R2] = {BindingKind::AxisPositive, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER};
    c.gamepad[L_UP] = {BindingKind::AxisNegative, SDL_GAMEPAD_AXIS_LEFTY};
    c.gamepad[L_RIGHT] = {BindingKind::AxisPositive, SDL_GAMEPAD_AXIS_LEFTX};
    c.gamepad[L_DOWN] = {BindingKind::AxisPositive, SDL_GAMEPAD_AXIS_LEFTY};
    c.gamepad[L_LEFT] = {BindingKind::AxisNegative, SDL_GAMEPAD_AXIS_LEFTX};
    c.gamepad[R_UP] = {BindingKind::AxisNegative, SDL_GAMEPAD_AXIS_RIGHTY};
    c.gamepad[R_RIGHT] = {BindingKind::AxisPositive, SDL_GAMEPAD_AXIS_RIGHTX};
    c.gamepad[R_DOWN] = {BindingKind::AxisPositive, SDL_GAMEPAD_AXIS_RIGHTY};
    c.gamepad[R_LEFT] = {BindingKind::AxisNegative, SDL_GAMEPAD_AXIS_RIGHTX};
    return c;
}
bool has_input_overrides(const Config& c) {
    const Config base = defaults();
    auto equal_binding = [](const Binding& a, const Binding& b) {
        return a.kind == b.kind && a.code == b.code;
    };
    return c.input != base.input || c.motion != base.motion ||
           !c.device_guid.empty() || !c.device_serial.empty() ||
           c.mouse_look != base.mouse_look || c.mouse_left_cross != base.mouse_left_cross ||
           c.mouse_sensitivity != base.mouse_sensitivity || c.deadzone != base.deadzone ||
           !std::equal(c.keyboard.begin(), c.keyboard.end(), base.keyboard.begin(), equal_binding) ||
           !std::equal(c.gamepad.begin(), c.gamepad.end(), base.gamepad.begin(), equal_binding);
}
std::wstring config_path() {
    wchar_t root[32768];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", root, 32768);
    if (!n || n >= 32768)
        return L"";
    return std::wstring(root) + L"\\BakuganRecomp\\player.ini";
}
static std::wstring get(const wchar_t *section, const wchar_t *key, const std::wstring &fallback,
                        const std::wstring &path) {
    wchar_t value[1024];
    DWORD n = GetPrivateProfileStringW(section, key, fallback.c_str(), value, 1024, path.c_str());
    return n < 1023 ? std::wstring(value, n) : fallback;
}
static float number(const std::wstring &s, float fallback, float lo, float hi) {
    wchar_t *end = nullptr;
    float v = wcstof(s.c_str(), &end);
    return end && end != s.c_str() && !*end && std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}
static std::wstring choose(const std::wstring &s, std::initializer_list<const wchar_t *> values,
                           const std::wstring &fallback) {
    for (auto v : values)
        if (s == v)
            return s;
    return fallback;
}
std::wstring binding_text(Binding b) {
    static const wchar_t *prefix[] = {L"none", L"key", L"mouse", L"button", L"axis+", L"axis-"};
    return std::wstring(prefix[(int)b.kind]) + L":" + std::to_wstring(b.code);
}
bool parse_binding(const std::wstring &s, Binding &out) {
    auto at = s.find(L':');
    if (at == std::wstring::npos)
        return false;
    std::wstring kind = s.substr(0, at);
    wchar_t *end = nullptr;
    long code = wcstol(s.c_str() + at + 1, &end, 10);
    if (end == s.c_str() + at + 1 || *end)
        return false;
    static const wchar_t *names[] = {L"none", L"key", L"mouse", L"button", L"axis+", L"axis-"};
    for (int i = 0; i < 6; ++i)
        if (kind == names[i]) {
            int max = i == 0   ? 0
                      : i == 1 ? 255
                      : i == 2 ? 5
                      : i == 3 ? SDL_GAMEPAD_BUTTON_COUNT - 1
                               : SDL_GAMEPAD_AXIS_COUNT - 1;
            if (code < 0 || code > max)
                return false;
            out = {(BindingKind)i, (int)code};
            return true;
        }
    return false;
}
bool load(Config &c, const std::wstring &path, std::wstring *error) {
    c = defaults();
    if (path.empty() || !std::filesystem::exists(path))
        return true;
    if (get(L"Player", L"Version", L"1", path) != L"1") {
        if (error)
            *error = L"Versión de configuración desconocida";
        return false;
    }
    c.output = choose(get(L"Graphics", L"Output", c.output, path),
                      {L"auto", L"native", L"720p", L"1080p", L"1440p", L"2160p"}, c.output);
    c.cap =
        choose(get(L"Graphics", L"FPS", c.cap, path), {L"0", L"30", L"60", L"90", L"120", L"monitor"}, c.cap);
    c.window = choose(get(L"Graphics", L"Window", c.window, path), {L"windowed", L"borderless"}, c.window);
    c.fsr = choose(get(L"Graphics", L"FSR", c.fsr, path), {L"auto", L"fsr1", L"0"}, c.fsr);
    c.language = choose(get(L"Player", L"Language", c.language, path),
                        {L"auto", L"en", L"fr", L"es", L"de", L"it", L"nl", L"sv"}, c.language);
    c.ui_language = choose(get(L"Player", L"LauncherLanguage", c.ui_language, path),
                           {L"auto", L"en", L"es"}, c.ui_language);
    c.sharpness = number(get(L"Graphics", L"Sharpness", L"0.6", path), 0.6f, 0, 2);
    int a = (int)number(get(L"Graphics", L"Anisotropy", L"16", path), 16, 1, 16);
    c.aniso = (a == 1 || a == 2 || a == 4 || a == 8 || a == 16) ? a : 16;
    c.input = (InputMode)(int)number(get(L"Input", L"Mode", L"0", path), 0, 0, 2);
    c.motion = (MotionMode)(int)number(get(L"Input", L"Motion", L"0", path), 0, 0, 2);
    c.deadzone = number(get(L"Input", L"Deadzone", L"0.15", path), 0.15f, 0.02f, 0.5f);
    c.mouse_sensitivity = number(get(L"Input", L"MouseSensitivity", L"0.5", path), 0.5f, 0.05f, 2);
    c.mouse_look = get(L"Input", L"MouseLook", L"1", path) != L"0";
    c.mouse_left_cross = get(L"Input", L"MouseLeftCross", L"1", path) != L"0";
    auto guid = get(L"Input", L"GUID", L"", path);
    auto serial = get(L"Input", L"Serial", L"", path);
    c.device_guid.assign(guid.begin(), guid.end());
    c.device_serial.assign(serial.begin(), serial.end());
    for (int i = 0; i < ACTION_COUNT; ++i) {
        auto key = std::to_wstring(i);
        Binding b;
        if (parse_binding(get(L"Keyboard", key.c_str(), binding_text(c.keyboard[i]), path), b))
            c.keyboard[i] = b;
        if (parse_binding(get(L"Gamepad", key.c_str(), binding_text(c.gamepad[i]), path), b))
            c.gamepad[i] = b;
    }
    return true;
}
bool save(const Config &c, const std::wstring &path, std::wstring *error) {
    if (path.empty()) {
        if (error)
            *error = L"No se pudo resolver la carpeta de ajustes";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (ec) {
        if (error)
            *error = L"No se pudo crear la carpeta de ajustes";
        return false;
    }
    if (!std::filesystem::exists(path)) {
        std::ofstream f(std::filesystem::path(path), std::ios::binary);
        f.put((char)0xff);
        f.put((char)0xfe);
        if (!f) {
            if (error)
                *error = L"No se pudo crear player.ini";
            return false;
        }
    }
    bool ok = true;
    auto put = [&](const wchar_t *section, const wchar_t *key, const std::wstring &value) {
        if (!WritePrivateProfileStringW(section, key, value.c_str(), path.c_str()))
            ok = false;
    };
    put(L"Player", L"Version", L"1");
    put(L"Player", L"Language", c.language);
    put(L"Player", L"LauncherLanguage", c.ui_language);
    put(L"Graphics", L"Output", c.output);
    put(L"Graphics", L"FPS", c.cap);
    put(L"Graphics", L"Window", c.window);
    put(L"Graphics", L"FSR", c.fsr);
    put(L"Graphics", L"Sharpness", std::to_wstring(c.sharpness));
    put(L"Graphics", L"Anisotropy", std::to_wstring(c.aniso));
    put(L"Input", L"Mode", std::to_wstring((int)c.input));
    put(L"Input", L"Motion", std::to_wstring((int)c.motion));
    put(L"Input", L"GUID", std::wstring(c.device_guid.begin(), c.device_guid.end()));
    put(L"Input", L"Serial", std::wstring(c.device_serial.begin(), c.device_serial.end()));
    put(L"Input", L"Deadzone", std::to_wstring(c.deadzone));
    put(L"Input", L"MouseSensitivity", std::to_wstring(c.mouse_sensitivity));
    put(L"Input", L"MouseLook", c.mouse_look ? L"1" : L"0");
    put(L"Input", L"MouseLeftCross", c.mouse_left_cross ? L"1" : L"0");
    for (int i = 0; i < ACTION_COUNT; ++i) {
        auto k = std::to_wstring(i);
        put(L"Keyboard", k.c_str(), binding_text(c.keyboard[i]));
        put(L"Gamepad", k.c_str(), binding_text(c.gamepad[i]));
    }
    if (!ok && error)
        *error = L"No se pudieron escribir todos los ajustes";
    return ok;
}
std::wstring binding_name(Binding b, bool english) {
    if (b.kind == BindingKind::None)
        return english ? L"Unassigned" : L"Sin asignar";
    if (b.kind == BindingKind::Mouse) {
        static const wchar_t *n[] = {L"",           L"Ratón izq.", L"Ratón centro",
                                     L"Ratón der.", L"Ratón X1",   L"Ratón X2"};
        static const wchar_t *n_en[] = {L"",            L"Mouse left", L"Mouse middle",
                                        L"Mouse right", L"Mouse X1",   L"Mouse X2"};
        return (english ? n_en : n)[std::clamp(b.code, 0, 5)];
    }
    if (b.kind == BindingKind::Key) {
        UINT scan = MapVirtualKeyW(b.code, MAPVK_VK_TO_VSC);
        if (b.code == VK_LEFT || b.code == VK_RIGHT || b.code == VK_UP || b.code == VK_DOWN ||
            b.code == VK_INSERT || b.code == VK_DELETE)
            scan |= 0x100;
        wchar_t name[80];
        if (GetKeyNameTextW((LONG)(scan << 16), name, 80))
            return name;
        return english ? L"Key " + std::to_wstring(b.code) : L"Tecla " + std::to_wstring(b.code);
    }
    if (b.kind == BindingKind::Button) {
        const char *n = SDL_GetGamepadStringForButton((SDL_GamepadButton)b.code);
        if (n)
            return std::wstring(n, n + strlen(n));
    }
    const char *a = SDL_GetGamepadStringForAxis((SDL_GamepadAxis)b.code);
    std::string n = a ? a : (english ? "axis" : "eje");
    return std::wstring(n.begin(), n.end()) + (b.kind == BindingKind::AxisNegative ? L" -" : L" +");
}
void apply_environment(const Config &c, const std::wstring &path) {
    auto env = [](const wchar_t *key, const std::wstring &value) {
        SetEnvironmentVariableW(key, value.empty() ? nullptr : value.c_str());
    };
    /* "Auto" with no device chosen is the fresh-install default. Leave it on
     * the port's old input path, which is the one the reference runs use; the
     * launcher bridge is opt-in by choosing a device or a manual mode. */
    const bool default_input = !has_input_overrides(c);
    env(L"PS3_PLAYER_CONFIG", default_input ? L"" : path);
    env(L"PS3_INTERNAL", L"720");
    env(L"PS3_OUTPUT", c.output);
    env(L"PS3_FSR", c.fsr);
    env(L"PS3_FSR_SHARPNESS", std::to_wstring(c.sharpness));
    env(L"PS3_ANISO", std::to_wstring(c.aniso));
    env(L"PS3_LOD_BIAS", L"0");
    /* Fixed shipping limit: the launcher no longer exposes an FPS selector. */
    env(L"PS3_FPS_CAP", L"30");
    env(L"PS3_TICK_HZ", L"");
    env(L"PS3_WINDOW_MODE", c.window);
    env(L"PS3_LANG", c.language == L"auto" ? L"" : c.language);
    env(L"PS3_VERBOSE", L"0");
    const wchar_t *probes[] = {L"PAD_RECORD",    L"PAD_REPLAY",      L"PAD_FILE",
                               L"PAD_AUTOPRESS", L"PAD_SCRIPT",      L"PAD_STICK",
                               L"PAD_SWEEP",     L"YDKJ_INJECT_PAD", L"PS3_SAVEDATA_CHOICE"};
    /* Do not forward a renderer diagnostic from whatever shell started the
     * launcher: these are documented to remove the HUD, cinematics or cards.
     * PS3_DEBUG=1 is the explicit opt-in for a diagnostic run. */
    {
        wchar_t debug[8];
        if (GetEnvironmentVariableW(L"PS3_DEBUG", debug, 8) == 0) {
            for (auto k : probes)
                SetEnvironmentVariableW(k, nullptr);
            static const wchar_t *const kBreaksImage[] = {
                L"SRV_SHADOW", L"DRAW_KEEP_TEX", L"DRAW_LAST_TEX", L"DRAW_KEEP_BLEND",
                L"TEX_MARKEMPTY", L"EMPTYTEX", L"DEPTH_OFF", L"TEX_RESOLVE_AUTO",
                L"PS3_NOVSYNC", L"SPU_CH_BLOCK"};
            for (auto k : kBreaksImage)
                SetEnvironmentVariableW(k, nullptr);
        }
    }
}
} // namespace player
