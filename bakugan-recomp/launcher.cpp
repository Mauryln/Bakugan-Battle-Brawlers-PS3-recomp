#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <string>
#include <cmath>
#include "player_config.h"
#include "gamepad_devices.h"
#include "launcher_theme.h"

namespace {
using namespace player;
enum {
    ID_OUTPUT = 100,
    ID_WINDOW,
    ID_SHARP,
    ID_ANISO,
    ID_LANG,
    ID_DEVICE,
    ID_MOTION,
    ID_GYRO_SENS,
    ID_DEADZONE,
    ID_SENSITIVITY,
    ID_MOUSE_LOOK,
    ID_MOUSE_CROSS,
    ID_MAP_MODE,
    ID_DEFAULTS,
    ID_SAVE,
    ID_PLAY,
    ID_UI_LANG,
    ID_GAMEPAD_CANVAS
};
constexpr UINT WM_APP_REBUILD = WM_APP + 1;
constexpr int WIDTH = 1000, HEIGHT = 800;
struct Control {
    HWND hwnd;
    int x, y, w, h;
};
std::vector<Control> controls;
HWND window = nullptr, status = nullptr, sensor_status = nullptr, gamepad_canvas = nullptr;
HFONT font = nullptr;
Config config;
Gamepads pads;
std::wstring path, directory;
std::string device_signature;
int capture = -1;
int selected_action = player::CROSS;
int hover_action = -1;
bool capture_armed = false;
ULONGLONG capture_start = 0;
bool ui_english = false;
const wchar_t *S(const wchar_t *es, const wchar_t *en) { return ui_english ? en : es; }

std::wstring localized(const std::wstring &error) {
    if (!ui_english)
        return error;
    if (error == L"Versión de configuración desconocida")
        return L"Unknown configuration version";
    if (error == L"No se pudo resolver la carpeta de ajustes")
        return L"Could not resolve the settings folder";
    if (error == L"No se pudo crear la carpeta de ajustes")
        return L"Could not create the settings folder";
    if (error == L"No se pudo crear player.ini")
        return L"Could not create player.ini";
    if (error == L"No se pudieron escribir todos los ajustes")
        return L"Could not write all settings";
    return error;
}

void apply_ui_language() {
    if (config.ui_language == L"en")
        ui_english = true;
    else if (config.ui_language == L"es")
        ui_english = false;
    else
        ui_english = PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_SPANISH;
    launcher_theme::set_english(ui_english);
}

const wchar_t *outputs[] = {L"720p", L"1080p", L"1440p", L"2160p", L"auto", L"native"};
const wchar_t *windows[] = {L"borderless", L"windowed"};
const wchar_t *languages[] = {L"auto", L"es", L"en", L"fr", L"de", L"it", L"nl", L"sv"};
const wchar_t *ui_languages[] = {L"auto", L"es", L"en"};

int selected(int id) { return (int)SendMessageW(GetDlgItem(window, id), CB_GETCURSEL, 0, 0); }
void message(const std::wstring &text) { SetWindowTextW(status, text.c_str()); }

HWND add(const wchar_t *cls, const wchar_t *text, int id, int x, int y, int w, int h, DWORD style = 0) {
    if (y >= 76)
        y += 70;
    if (!wcscmp(cls, L"BUTTON") && (style & BS_TYPEMASK) == BS_GROUPBOX) {
        cls = L"STATIC";
        style = 0;
        x += 12;
        y += 5;
        w -= 24;
        h = 18;
    }
    if (!wcscmp(cls, L"BUTTON") &&
        ((style & BS_TYPEMASK) == BS_PUSHBUTTON || (style & BS_TYPEMASK) == BS_DEFPUSHBUTTON))
        style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW;
    HWND child = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window,
                                 (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    controls.push_back({child, x, y, w, h});
    return child;
}

HWND combo(int id, int x, int y, int w, std::initializer_list<const wchar_t *> items) {
    HWND child = add(L"COMBOBOX", L"", id, x, y, w, 230, CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL);
    for (auto item : items)
        SendMessageW(child, CB_ADDSTRING, 0, (LPARAM)item);
    SendMessageW(child, CB_SETCURSEL, 0, 0);
    return child;
}

void label(const wchar_t *text, int x, int y, int w = 260) { add(L"STATIC", text, 0, x, y, w, 22); }
void choose(int id, int index) { SendMessageW(GetDlgItem(window, id), CB_SETCURSEL, std::max(0, index), 0); }
template <size_t N> void choose(int id, const wchar_t *const (&values)[N], const std::wstring &value) {
    int found = 0;
    for (size_t i = 0; i < N; ++i)
        if (value == values[i])
            found = (int)i;
    choose(id, found);
}
void check(int id, bool checked) {
    SendMessageW(GetDlgItem(window, id), BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}
bool checked(int id) { return SendMessageW(GetDlgItem(window, id), BM_GETCHECK, 0, 0) == BST_CHECKED; }
void slider(int id, int x, int y, int w, int lo, int hi, int value) {
    HWND c = add(TRACKBAR_CLASSW, L"", id, x, y, w, 28, WS_TABSTOP | TBS_NOTICKS);
    SendMessageW(c, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
    SendMessageW(c, TBM_SETPOS, TRUE, value);
}
int position(int id) { return (int)SendMessageW(GetDlgItem(window, id), TBM_GETPOS, 0, 0); }

void update_bindings() {
    if (gamepad_canvas)
        InvalidateRect(gamepad_canvas, nullptr, FALSE);
}

void read_device() {
    int i = selected(ID_DEVICE);
    if (i == 0) {
        config.device_guid.clear();
        config.device_serial.clear();
    } else if (i > 0 && i <= (int)pads.devices().size()) {
        const auto &d = pads.devices()[i - 1];
        config.device_guid = d.guid;
        config.device_serial = d.serial;
    }
}

void refresh_devices(bool force = false) {
    pads.refresh();
    std::string signature;
    for (const auto &d : pads.devices())
        signature += std::to_string(d.id) + d.name + d.guid + d.serial;
    if (force || signature != device_signature) {
        device_signature = signature;
        HWND box = GetDlgItem(window, ID_DEVICE);
        SendMessageW(box, CB_RESETCONTENT, 0, 0);
        SendMessageW(box, CB_ADDSTRING, 0,
                     (LPARAM)S(L"Automático: primer mando conectado",
                              L"Automatic: first connected controller"));
        int index = 0;
        for (size_t i = 0; i < pads.devices().size(); ++i) {
            const auto &d = pads.devices()[i];
            auto name = utf8_wide(d.name);
            SendMessageW(box, CB_ADDSTRING, 0, (LPARAM)name.c_str());
            if (d.guid == config.device_guid &&
                (config.device_serial.empty() || d.serial == config.device_serial))
                index = (int)i + 1;
        }
        if (!config.device_guid.empty() && !index) {
            index = (int)pads.devices().size() + 1;
            SendMessageW(box, CB_ADDSTRING, 0,
                         (LPARAM)S(L"Mando guardado (desconectado)",
                                  L"Saved controller (disconnected)"));
        }
        choose(ID_DEVICE, index);
    }
    Device *device = pads.selected(config);
    std::wstring info = S(L"Sin mando conectado. Sixaxis: palanca R / teclas I J K L.",
                          L"No controller connected. Sixaxis: right stick / keys I J K L.");
    if (device) {
        std::array<float, 3> a{}, g{};
        bool real = selected(ID_MOTION) != 2 && pads.motion(device, a, g);
        info = real ? S(L"Giroscopio activo -> Stick derecho virtual + Sixaxis.",
                        L"Gyroscope active -> Virtual right stick + Sixaxis.")
                    : S(L"Sensores no disponibles o desactivados: Stick derecho.",
                        L"Sensors unavailable or disabled: Right stick Sixaxis.");
    }
    SetWindowTextW(sensor_status, info.c_str());
}

void read_controls() {
    auto index = [](int id, int count) { return std::clamp(selected(id), 0, count - 1); };
    config.output = outputs[index(ID_OUTPUT, 6)];
    config.cap = L"30";
    config.window = windows[index(ID_WINDOW, 2)];
    config.fsr = L"auto";
    config.sharpness = position(ID_SHARP) / 100.f;
    const int aniso[] = {1, 2, 4, 8, 16};
    config.aniso = aniso[index(ID_ANISO, 5)];
    config.language = languages[index(ID_LANG, 8)];
    config.ui_language = ui_languages[index(ID_UI_LANG, 3)];
    config.input = InputMode::Auto;
    config.motion = (MotionMode)index(ID_MOTION, 3);
    config.gyro_sensitivity = position(ID_GYRO_SENS) / 100.f;
    config.deadzone = position(ID_DEADZONE) / 100.f;
    config.mouse_sensitivity = position(ID_SENSITIVITY) / 100.f;
    config.mouse_look = checked(ID_MOUSE_LOOK);
    config.mouse_left_cross = checked(ID_MOUSE_CROSS);
    read_device();
}

void fill_controls() {
    choose(ID_OUTPUT, outputs, config.output);
    choose(ID_WINDOW, windows, config.window);
    choose(ID_LANG, languages, config.language);
    choose(ID_UI_LANG, ui_languages, config.ui_language);
    choose(ID_MOTION, (int)config.motion);
    int a = 0;
    for (int n = config.aniso; n > 1; n /= 2)
        ++a;
    choose(ID_ANISO, a);
    SendMessageW(GetDlgItem(window, ID_SHARP), TBM_SETPOS, TRUE, (int)(config.sharpness * 100));
    SendMessageW(GetDlgItem(window, ID_DEADZONE), TBM_SETPOS, TRUE, (int)(config.deadzone * 100));
    SendMessageW(GetDlgItem(window, ID_GYRO_SENS), TBM_SETPOS, TRUE, (int)(config.gyro_sensitivity * 100));
    SendMessageW(GetDlgItem(window, ID_SENSITIVITY), TBM_SETPOS, TRUE, (int)(config.mouse_sensitivity * 100));
    check(ID_MOUSE_LOOK, config.mouse_look);
    check(ID_MOUSE_CROSS, config.mouse_left_cross);
    update_bindings();
    refresh_devices(true);
}

void layout() {
    RECT client;
    GetClientRect(window, &client);
    double scale = std::min((double)client.right / WIDTH, (double)client.bottom / HEIGHT);
    if (font)
        DeleteObject(font);
    font =
        CreateFontW(-(int)std::lround(15 * scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    for (const auto &c : controls) {
        MoveWindow(c.hwnd, (int)std::lround(c.x * scale), (int)std::lround(c.y * scale),
                   (int)std::lround(c.w * scale), (int)std::lround(c.h * scale), TRUE);
        SendMessageW(c.hwnd, WM_SETFONT, (WPARAM)font, TRUE);
    }
}

void fit_window(UINT dpi, const RECT *suggested = nullptr) {
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info);
    RECT r{0, 0, MulDiv(WIDTH, dpi, 96), MulDiv(HEIGHT, dpi, 96)};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0, dpi);
    int w = std::min(r.right - r.left, info.rcWork.right - info.rcWork.left);
    int h = std::min(r.bottom - r.top, info.rcWork.bottom - info.rcWork.top);
    int x = suggested ? suggested->left : info.rcWork.left + (info.rcWork.right - info.rcWork.left - w) / 2;
    int y = suggested ? suggested->top : info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - h) / 2;
    x = std::clamp<long>(x, info.rcWork.left, info.rcWork.right - w);
    y = std::clamp<long>(y, info.rcWork.top, info.rcWork.bottom - h);
    SetWindowPos(window, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    layout();
}

bool save_settings() {
    read_controls();
    std::wstring error;
    if (!save(config, path, &error)) {
        message(localized(error));
        return false;
    }
    message(S(L"Ajustes guardados. Se aplicarán al iniciar el juego.",
              L"Settings saved. They will apply when the game starts."));
    return true;
}

void launch() {
    if (!save_settings())
        return;
    std::filesystem::path exe = std::filesystem::path(directory) / L"BakuganRecomp.exe";
    if (!std::filesystem::exists(exe)) {
        message(S(L"No se encuentra BakuganRecomp.exe junto al launcher.",
                  L"BakuganRecomp.exe was not found next to the launcher."));
        return;
    }
    std::filesystem::path elf;
    wchar_t vfs[32768];
    DWORD n = GetEnvironmentVariableW(L"PS3_VFS_ROOT", vfs, 32768);
    if (n && n < 32768)
        elf = std::filesystem::path(vfs) / L"PS3_GAME/USRDIR/EBOOT.elf";
    for (const auto &candidate : {std::filesystem::path(directory) / L"../../game/PS3_GAME/USRDIR/EBOOT.elf",
                                  std::filesystem::path(directory) / L"game/PS3_GAME/USRDIR/EBOOT.elf"})
        if (!std::filesystem::exists(elf) && std::filesystem::exists(candidate))
            elf = candidate;
    if (!std::filesystem::exists(elf)) {
        wchar_t file[32768]{};
        OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.hwndOwner = window;
        dialog.lpstrFilter = S(L"Ejecutable del juego (EBOOT.elf)\0EBOOT.elf\0",
                               L"Game executable (EBOOT.elf)\0EBOOT.elf\0");
        dialog.lpstrFile = file;
        dialog.nMaxFile = 32768;
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&dialog)) {
            message(S(L"Selecciona EBOOT.elf para iniciar.", L"Select EBOOT.elf to start."));
            return;
        }
        elf = file;
    }
    elf = std::filesystem::absolute(elf).lexically_normal();
    if (elf.parent_path().filename() != L"USRDIR" ||
        elf.parent_path().parent_path().filename() != L"PS3_GAME") {
        message(S(L"EBOOT.elf debe estar dentro de PS3_GAME/USRDIR.",
                  L"EBOOT.elf must be inside PS3_GAME/USRDIR."));
        return;
    }
    apply_environment(config, path);
    SetEnvironmentVariableW(L"PS3_PLAYER_CONFIG", path.c_str());
    SetEnvironmentVariableW(L"PS3_VFS_ROOT", elf.parent_path().parent_path().parent_path().c_str());
    SetEnvironmentVariableW(L"PS3_TITLE", L"Bakugan: Battle Brawlers");
    std::wstring command = L"\"" + exe.wstring() + L"\" \"" + elf.wstring() + L"\"";
    std::wstring log_path = (std::filesystem::path(path).parent_path() / L"BakuganRecomp.log").wstring();
    std::error_code log_ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), log_ec);
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE log = CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW startup{sizeof(startup)};
    if (log != INVALID_HANDLE_VALUE) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = log;
        startup.hStdError = log;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    PROCESS_INFORMATION process{};
    BOOL started = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr,
                                  log != INVALID_HANDLE_VALUE, 0, nullptr, directory.c_str(),
                                  &startup, &process);
    if (!started) {
        DWORD error = GetLastError();
        if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
        message(S(L"No se pudo iniciar el juego. Error ", L"The game could not start. Error ") +
                std::to_wstring(error));
        return;
    }
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    PostMessageW(window, WM_CLOSE, 0, 0);
}

void finish_capture(Binding binding, bool cancel = false) {
    if (!cancel && capture >= 0 && capture < ACTION_COUNT) {
        auto &map = selected(ID_MAP_MODE) == 1 ? config.gamepad : config.keyboard;
        map[capture] = binding;
    }
    capture = -1;
    capture_armed = false;
    update_bindings();
    message(cancel ? S(L"Asignación cancelada.", L"Binding cancelled.")
                   : S(L"Asignación aplicada. Guarda los ajustes para conservarla.",
                       L"Binding applied. Save settings to preserve it."));
}

void poll_capture() {
    if (capture < 0)
        return;
    if (GetForegroundWindow() != window)
        return;
    if (GetTickCount64() - capture_start > 15000) {
        finish_capture({}, true);
        return;
    }
    Device *device = pads.selected(config);
    SDL_Gamepad *pad = device ? device->handle : nullptr;
    bool gamepad = selected(ID_MAP_MODE) == 1, held = false;
    for (int key = 1; key < 256; ++key)
        if (GetAsyncKeyState(key) & 0x8000)
            held = true;
    if (gamepad && pad) {
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
            if (SDL_GetGamepadButton(pad, (SDL_GamepadButton)b))
                held = true;
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
            if (std::abs((int)SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)a)) > 16000)
                held = true;
    }
    if (!capture_armed) {
        if (!held)
            capture_armed = true;
        return;
    }
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
        finish_capture({}, true);
        return;
    }
    if (GetAsyncKeyState(VK_DELETE) & 0x8000) {
        finish_capture({});
        return;
    }
    if (gamepad) {
        if (!pad)
            return;
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
            if (SDL_GetGamepadButton(pad, (SDL_GamepadButton)b)) {
                finish_capture({BindingKind::Button, b});
                return;
            }
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) {
            int value = SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)a);
            if (std::abs(value) > 22000) {
                finish_capture({value < 0 ? BindingKind::AxisNegative : BindingKind::AxisPositive, a});
                return;
            }
        }
    } else {
        const int mouse[] = {VK_LBUTTON, VK_MBUTTON, VK_RBUTTON, VK_XBUTTON1, VK_XBUTTON2};
        for (int i = 0; i < 5; ++i)
            if (GetAsyncKeyState(mouse[i]) & 0x8000) {
                finish_capture({BindingKind::Mouse, i + 1});
                return;
            }
        for (int key = 8; key < 256; ++key) {
            if (key == VK_LSHIFT || key == VK_RSHIFT || key == VK_LCONTROL || key == VK_RCONTROL ||
                key == VK_LMENU || key == VK_RMENU)
                continue;
            if (GetAsyncKeyState(key) & 0x8000) {
                finish_capture({BindingKind::Key, key});
                return;
            }
        }
    }
}

bool capture_message(const MSG &msg) {
    if (capture < 0 || !capture_armed || selected(ID_MAP_MODE) == 1)
        return false;
    if (msg.message == WM_KEYDOWN) {
        if (msg.wParam == VK_ESCAPE)
            finish_capture({}, true);
        else if (msg.wParam == VK_DELETE)
            finish_capture({});
        else
            finish_capture({BindingKind::Key, (int)msg.wParam});
        return true;
    }
    int mouse = msg.message == WM_LBUTTONDOWN   ? 1
                : msg.message == WM_MBUTTONDOWN ? 2
                : msg.message == WM_RBUTTONDOWN ? 3
                : msg.message == WM_XBUTTONDOWN ? (GET_XBUTTON_WPARAM(msg.wParam) == XBUTTON1 ? 4 : 5)
                                                : 0;
    if (mouse) {
        finish_capture({BindingKind::Mouse, mouse});
        return true;
    }
    return false;
}

bool activate_button(const MSG &msg) {
    if (capture >= 0 || msg.message != WM_KEYDOWN || msg.wParam != VK_RETURN)
        return false;
    wchar_t class_name[32];
    if (!GetClassNameW(msg.hwnd, class_name, 32) || wcscmp(class_name, L"Button") ||
        (GetWindowLongPtrW(msg.hwnd, GWL_STYLE) & BS_TYPEMASK) != BS_OWNERDRAW)
        return false;
    SendMessageW(msg.hwnd, BM_CLICK, 0, 0);
    return true;
}

// Visual Gamepad Controller Data & Rendering
struct ButtonHotspot {
    int action;
    const wchar_t *ps_name;
    const wchar_t *desc_es;
    const wchar_t *desc_en;
    float x, y, w, h;
    bool is_circle;
};

static const ButtonHotspot kButtons[player::ACTION_COUNT] = {
    {player::SELECT, L"SELECT", L"Seleccionar / Mapa", L"Select / Map", 232.f, 118.f, 40.f, 18.f, false},
    {player::L3, L"L3", L"Stick izquierdo (pulsar)", L"Left stick click", 199.f, 199.f, 30.f, 30.f, true},
    {player::R3, L"R3", L"Stick derecho (pulsar)", L"Right stick click", 399.f, 199.f, 30.f, 30.f, true},
    {player::START, L"START", L"Pausa / Opciones", L"Pause / Start", 356.f, 118.f, 40.f, 18.f, false},
    {player::UP, L"▲", L"Cruceta arriba", L"D-Pad up", 136.f, 106.f, 26.f, 24.f, false},
    {player::RIGHT, L"▶", L"Cruceta derecha", L"D-Pad right", 162.f, 128.f, 24.f, 24.f, false},
    {player::DOWN, L"▼", L"Cruceta abajo", L"D-Pad down", 136.f, 150.f, 26.f, 24.f, false},
    {player::LEFT, L"◀", L"Cruceta izquierda", L"D-Pad left", 112.f, 128.f, 24.f, 24.f, false},
    {player::L2, L"L2", L"Gatillo L2 analógico", L"Left trigger L2", 116.f, 18.f, 66.f, 22.f, false},
    {player::R2, L"R2", L"Gatillo R2 analógico", L"Right trigger R2", 446.f, 18.f, 66.f, 22.f, false},
    {player::L1, L"L1", L"Botón superior L1", L"Left shoulder L1", 116.f, 46.f, 66.f, 20.f, false},
    {player::R1, L"R1", L"Botón superior R1", L"Right shoulder R1", 446.f, 46.f, 66.f, 20.f, false},
    {player::TRIANGLE, L"△", L"Triángulo / Acción superior", L"Triangle / North action", 466.f, 106.f, 26.f, 26.f, true},
    {player::CIRCLE, L"○", L"Círculo / Cancelar", L"Circle / East action", 492.f, 128.f, 26.f, 26.f, true},
    {player::CROSS, L"✕", L"Cruz / Aceptar / Salto", L"Cross / South action", 466.f, 150.f, 26.f, 26.f, true},
    {player::SQUARE, L"□", L"Cuadrado / Ataque", L"Square / West action", 440.f, 128.f, 26.f, 26.f, true},
    {player::MOTION_THROW, L"SIX", L"Impulso Sixaxis / Lanzar", L"Sixaxis impulse / Throw", 300.f, 114.f, 28.f, 28.f, true},
    {player::L_UP, L"L▲", L"Stick L arriba", L"Left stick up", 203.f, 175.f, 22.f, 20.f, false},
    {player::L_RIGHT, L"L▶", L"Stick L derecha", L"Left stick right", 233.f, 204.f, 20.f, 20.f, false},
    {player::L_DOWN, L"L▼", L"Stick L abajo", L"Left stick down", 203.f, 233.f, 22.f, 20.f, false},
    {player::L_LEFT, L"L◀", L"Stick L izquierda", L"Left stick left", 175.f, 204.f, 20.f, 20.f, false},
    {player::R_UP, L"R▲", L"Stick R arriba", L"Right stick up", 403.f, 175.f, 22.f, 20.f, false},
    {player::R_RIGHT, L"R▶", L"Stick R derecha", L"Right stick right", 433.f, 204.f, 20.f, 20.f, false},
    {player::R_DOWN, L"R▼", L"Stick R abajo", L"Right stick down", 403.f, 233.f, 22.f, 20.f, false},
    {player::R_LEFT, L"R◀", L"Stick R izquierda", L"Right stick left", 375.f, 204.f, 20.f, 20.f, false}
};

struct ConflictResult {
    bool conflict = false;
    int other_action = -1;
};

ConflictResult check_conflict(int action, bool is_gamepad) {
    ConflictResult res{};
    const auto &map = is_gamepad ? config.gamepad : config.keyboard;
    const auto &target = map[action];
    if (target.kind == BindingKind::None)
        return res;
    for (int i = 0; i < ACTION_COUNT; ++i) {
        if (i == action)
            continue;
        if (map[i].kind == target.kind && map[i].code == target.code) {
            res.conflict = true;
            res.other_action = i;
            return res;
        }
    }
    return res;
}

int hit_test_button(float mx, float my) {
    for (int i = 0; i < ACTION_COUNT; ++i) {
        const auto &b = kButtons[i];
        if (b.is_circle) {
            float cx = b.x + b.w / 2.f, cy = b.y + b.h / 2.f;
            float r = b.w / 2.f;
            float dx = mx - cx, dy = my - cy;
            if (dx * dx + dy * dy <= r * r)
                return b.action;
        } else {
            if (mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h)
                return b.action;
        }
    }
    return -1;
}

void draw_gamepad(Gdiplus::Graphics &g, int width, int height) {
    using namespace Gdiplus;
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    float sx = width / 628.0f;
    float sy = height / 396.0f;
    g.ScaleTransform(sx, sy);

    // Canvas background container
    SolidBrush bg_brush(Color(255, 12, 18, 30));
    g.FillRectangle(&bg_brush, RectF(0.f, 0.f, 628.f, 396.f));
    Pen border_pen(Color(255, 38, 56, 82), 1.0f);
    g.DrawRectangle(&border_pen, RectF(0.5f, 0.5f, 627.f, 395.f));

    // Gamepad controller body silhouette
    SolidBrush body_brush(Color(255, 20, 30, 48));
    Pen body_pen(Color(255, 55, 82, 118), 1.5f);

    // Center bridge
    g.FillRectangle(&body_brush, RectF(180.f, 82.f, 268.f, 130.f));
    g.DrawRectangle(&body_pen, RectF(180.f, 82.f, 268.f, 130.f));

    // Left grip
    PointF left_grip[6] = {
        PointF(110.f, 78.f), PointF(180.f, 82.f), PointF(180.f, 212.f),
        PointF(145.f, 282.f), PointF(92.f, 270.f), PointF(90.f, 120.f)
    };
    g.FillPolygon(&body_brush, left_grip, 6);
    g.DrawPolygon(&body_pen, left_grip, 6);

    // Right grip
    PointF right_grip[6] = {
        PointF(448.f, 82.f), PointF(518.f, 78.f), PointF(538.f, 120.f),
        PointF(536.f, 270.f), PointF(483.f, 282.f), PointF(448.f, 212.f)
    };
    g.FillPolygon(&body_brush, right_grip, 6);
    g.DrawPolygon(&body_pen, right_grip, 6);

    // Sticks base wells
    SolidBrush stick_well(Color(255, 15, 22, 34));
    Pen stick_well_pen(Color(255, 45, 68, 98), 1.2f);
    g.FillEllipse(&stick_well, RectF(170.f, 170.f, 88.f, 88.f));
    g.DrawEllipse(&stick_well_pen, RectF(170.f, 170.f, 88.f, 88.f));
    g.FillEllipse(&stick_well, RectF(370.f, 170.f, 88.f, 88.f));
    g.DrawEllipse(&stick_well_pen, RectF(370.f, 170.f, 88.f, 88.f));

    // D-Pad cross plate
    SolidBrush dpad_base(Color(255, 16, 24, 38));
    g.FillRectangle(&dpad_base, RectF(134.f, 104.f, 30.f, 72.f));
    g.FillRectangle(&dpad_base, RectF(110.f, 126.f, 78.f, 28.f));

    bool is_gamepad = selected(ID_MAP_MODE) == 1;
    const auto &bindings = is_gamepad ? config.gamepad : config.keyboard;

    Font sym_font(L"Segoe UI", 9.5f, FontStyleBold);
    Font badge_font(L"Segoe UI", 7.5f, FontStyleRegular);
    StringFormat sf_center;
    sf_center.SetAlignment(StringAlignmentCenter);
    sf_center.SetLineAlignment(StringAlignmentCenter);

    for (int i = 0; i < ACTION_COUNT; ++i) {
        const auto &b = kButtons[i];
        bool is_sel = (selected_action == b.action);
        bool is_hov = (hover_action == b.action);
        bool is_cap = (capture == b.action);
        ConflictResult cr = check_conflict(b.action, is_gamepad);

        Color fill_col = is_cap ? Color(255, 95, 75, 20)
                       : is_sel ? Color(255, 70, 52, 16)
                       : is_hov ? Color(255, 28, 62, 100)
                       : cr.conflict ? Color(255, 80, 25, 30)
                       : Color(255, 26, 38, 58);

        Color border_col = is_cap ? Color(255, 238, 193, 97)
                         : is_sel ? Color(255, 238, 193, 97)
                         : cr.conflict ? Color(255, 240, 65, 65)
                         : is_hov ? Color(255, 75, 170, 255)
                         : Color(255, 65, 95, 135);

        float pen_w = (is_sel || is_cap || cr.conflict) ? 2.0f : 1.0f;
        SolidBrush btn_brush(fill_col);
        Pen btn_pen(border_col, pen_w);

        RectF r(b.x, b.y, b.w, b.h);
        if (b.is_circle) {
            g.FillEllipse(&btn_brush, r);
            g.DrawEllipse(&btn_pen, r);
        } else {
            g.FillRectangle(&btn_brush, r);
            g.DrawRectangle(&btn_pen, r);
        }

        // Color for PlayStation Symbols
        Color sym_col = Color(255, 240, 245, 255);
        if (b.action == player::TRIANGLE)
            sym_col = Color(255, 70, 220, 120);
        else if (b.action == player::CIRCLE)
            sym_col = Color(255, 245, 70, 70);
        else if (b.action == player::CROSS)
            sym_col = Color(255, 65, 180, 255);
        else if (b.action == player::SQUARE)
            sym_col = Color(255, 240, 110, 190);
        else if (b.action == player::MOTION_THROW)
            sym_col = Color(255, 238, 193, 97);

        SolidBrush sym_brush(sym_col);
        g.DrawString(b.ps_name, -1, &sym_font, r, &sf_center, &sym_brush);

        // Conflict exclamation badge
        if (cr.conflict) {
            SolidBrush warn_bg(Color(255, 230, 50, 50));
            g.FillEllipse(&warn_bg, RectF(b.x + b.w - 8.f, b.y - 4.f, 10.f, 10.f));
            SolidBrush warn_txt(Color(255, 255, 255, 255));
            Font warn_font(L"Segoe UI", 6.5f, FontStyleBold);
            g.DrawString(L"!", -1, &warn_font, RectF(b.x + b.w - 8.f, b.y - 4.f, 10.f, 10.f), &sf_center, &warn_txt);
        }
    }

    // Inspector Panel Card at the bottom (y = 310 .. 386)
    SolidBrush panel_bg(Color(255, 14, 22, 36));
    Pen panel_border(Color(255, 42, 64, 94), 1.0f);
    g.FillRectangle(&panel_bg, RectF(12.f, 308.f, 604.f, 78.f));
    g.DrawRectangle(&panel_border, RectF(12.f, 308.f, 604.f, 78.f));

    int cur_act = (selected_action >= 0 && selected_action < ACTION_COUNT) ? selected_action : player::CROSS;
    const auto &cur_b = kButtons[cur_act];
    ConflictResult cur_cr = check_conflict(cur_act, is_gamepad);

    Font head_font(L"Segoe UI", 11.f, FontStyleBold);
    Font sub_font(L"Segoe UI", 9.f, FontStyleRegular);
    Font val_font(L"Segoe UI", 10.5f, FontStyleBold);

    // Left info sub-box
    SolidBrush gold_txt(Color(255, 238, 193, 97));
    SolidBrush white_txt(Color(255, 240, 245, 255));
    SolidBrush dim_txt(Color(255, 160, 180, 205));

    std::wstring title = std::wstring(cur_b.ps_name) + L" — " + (ui_english ? cur_b.desc_en : cur_b.desc_es);
    g.DrawString(title.c_str(), -1, &head_font, PointF(24.f, 316.f), &gold_txt);

    std::wstring b_text = binding_name(bindings[cur_act], ui_english);
    std::wstring bind_label = S(L"Asignación actual: [ ", L"Current binding: [ ") + b_text + L" ]";
    g.DrawString(bind_label.c_str(), -1, &val_font, PointF(24.f, 342.f), &white_txt);

    std::wstring dev_mode_str = is_gamepad ? S(L"Editando Mando (SDL3)", L"Editing Gamepad (SDL3)")
                                           : S(L"Editando Teclado y Ratón", L"Editing Keyboard & Mouse");
    g.DrawString(dev_mode_str.c_str(), -1, &sub_font, PointF(24.f, 363.f), &dim_txt);

    // Right status / alert sub-box (x = 340 .. 604)
    RectF status_card(336.f, 314.f, 272.f, 66.f);
    if (capture == cur_act) {
        SolidBrush cap_bg(Color(255, 65, 50, 15));
        Pen cap_pen(Color(255, 238, 193, 97), 1.5f);
        g.FillRectangle(&cap_bg, status_card);
        g.DrawRectangle(&cap_pen, status_card);

        SolidBrush cap_head(Color(255, 255, 220, 120));
        g.DrawString(S(L"⏳ PULSA UNA ENTRADA...", L"⏳ PRESS AN INPUT..."), -1, &head_font, PointF(344.f, 320.f), &cap_head);
        g.DrawString(S(L"Pulsa una tecla, botón o eje.\nEsc cancela · Supr desasigna.",
                       L"Press a key, button, or axis.\nEsc cancels · Del clears."),
                     -1, &sub_font, PointF(344.f, 340.f), &white_txt);
    } else if (cur_cr.conflict) {
        SolidBrush cr_bg(Color(255, 65, 20, 25));
        Pen cr_pen(Color(255, 240, 60, 60), 1.5f);
        g.FillRectangle(&cr_bg, status_card);
        g.DrawRectangle(&cr_pen, status_card);

        SolidBrush cr_head(Color(255, 255, 90, 90));
        g.DrawString(S(L"⚠️ CONFLICTO DE ASIGNACIÓN", L"⚠️ BINDING CONFLICT"), -1, &head_font, PointF(344.f, 320.f), &cr_head);
        std::wstring other_name = action_name(cur_cr.other_action, ui_english);
        std::wstring cr_desc = S(L"Coincide con: ", L"Clashes with: ") + other_name;
        g.DrawString(cr_desc.c_str(), -1, &sub_font, PointF(344.f, 342.f), &white_txt);
    } else {
        SolidBrush ok_head(Color(255, 80, 210, 130));
        g.DrawString(S(L"✓ ASIGNACIÓN LISTA", L"✓ READY"), -1, &head_font, PointF(344.f, 320.f), &ok_head);
        g.DrawString(S(L"Haz clic en cualquier botón del\nmando para reasignarlo.",
                       L"Click any button on the gamepad\nto rebind it."),
                     -1, &sub_font, PointF(344.f, 340.f), &dim_txt);
    }
}

LRESULT CALLBACK canvas_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        int w = r.right - r.left, h = r.bottom - r.top;
        if (w > 0 && h > 0) {
            HDC mem_dc = CreateCompatibleDC(dc);
            HBITMAP mem_bm = CreateCompatibleBitmap(dc, w, h);
            HGDIOBJ old_bm = SelectObject(mem_dc, mem_bm);
            {
                Gdiplus::Graphics g(mem_dc);
                draw_gamepad(g, w, h);
            }
            BitBlt(dc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);
            SelectObject(mem_dc, old_bm);
            DeleteObject(mem_bm);
            DeleteDC(mem_dc);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        RECT r;
        GetClientRect(hwnd, &r);
        float sx = r.right > 0 ? (628.0f / r.right) : 1.0f;
        float sy = r.bottom > 0 ? (396.0f / r.bottom) : 1.0f;
        float mx = GET_X_LPARAM(lp) * sx;
        float my = GET_Y_LPARAM(lp) * sy;
        int hit = hit_test_button(mx, my);
        if (hit != hover_action) {
            hover_action = hit;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        SetCursor(LoadCursor(nullptr, hit >= 0 ? IDC_HAND : IDC_ARROW));
        return 0;
    }
    case WM_LBUTTONDOWN: {
        RECT r;
        GetClientRect(hwnd, &r);
        float sx = r.right > 0 ? (628.0f / r.right) : 1.0f;
        float sy = r.bottom > 0 ? (396.0f / r.bottom) : 1.0f;
        float mx = GET_X_LPARAM(lp) * sx;
        float my = GET_Y_LPARAM(lp) * sy;
        int hit = hit_test_button(mx, my);
        if (hit >= 0) {
            selected_action = hit;
            capture = hit;
            capture_start = GetTickCount64();
            capture_armed = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            SetFocus(window);
            message(S(L"Suelta los controles y pulsa una tecla, botón o eje. Esc cancela; Supr borra.",
                      L"Release controls and press a key, button, or axis. Esc cancels; Del clears."));
        }
        return 0;
    }
    case WM_SETCURSOR:
        if (hover_action >= 0) {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void create_controls() {
    apply_ui_language();
    label(S(L"Idioma del launcher", L"Launcher language"), 712, 26, 272);
    combo(ID_UI_LANG, 712, 46, 272,
          {S(L"Automático (Windows)", L"Automatic (Windows)"), L"Español", L"English"});

    // Panel izquierdo: Pantalla y gráficos
    add(L"BUTTON", S(L"Pantalla y gráficos", L"Display and graphics"), 0, 16, 76, 302, 558, BS_GROUPBOX);
    label(S(L"Resolución de salida", L"Output resolution"), 32, 92);
    combo(ID_OUTPUT, 32, 114, 268,
          {L"1280 × 720", L"1920 × 1080", L"2560 × 1440", L"3840 × 2160",
           S(L"Automática", L"Automatic"), S(L"Resolución del monitor", L"Monitor resolution")});
    label(S(L"Modo de ventana", L"Window mode"), 32, 154);
    combo(ID_WINDOW, 32, 176, 268,
          {S(L"Sin bordes (Borderless)", L"Borderless windowed"), S(L"Ventana", L"Windowed")});
    label(S(L"Filtrado anisotrópico", L"Anisotropic filtering"), 32, 216);
    combo(ID_ANISO, 32, 238, 268, {L"1×", L"2×", L"4×", L"8×", L"16×"});
    label(S(L"Nitidez: 0 alta · 2 baja", L"Image sharpness: 0 sharp · 2 soft"), 32, 278);
    slider(ID_SHARP, 28, 300, 276, 0, 200, 60);

    add(L"STATIC",
        S(L"El juego se ejecuta a 30 FPS con frame pacing uniforme y FSR1 automático en resoluciones superiores a 720p.",
          L"The game runs at 30 FPS with uniform frame pacing and automatic FSR1 above 720p."),
        0, 32, 346, 268, 54);

    add(L"BUTTON", S(L"Mantener clic derecho: apuntar", L"Hold right click: aim"), ID_MOUSE_LOOK, 32, 420,
        276, 24, BS_AUTOCHECKBOX | WS_TABSTOP);
    add(L"BUTTON", S(L"Clic izquierdo: Cruz (X)", L"Left click: Cross (X)"), ID_MOUSE_CROSS, 32, 448, 276,
        24, BS_AUTOCHECKBOX | WS_TABSTOP);
    label(S(L"Sensibilidad del ratón", L"Mouse sensitivity"), 32, 484);
    slider(ID_SENSITIVITY, 28, 508, 276, 5, 200, 50);

    // Panel derecho: Idioma y controles
    add(L"BUTTON", S(L"Idioma y controles interactivos", L"Language and interactive controls"), 0, 332, 76, 652, 558, BS_GROUPBOX);
    label(S(L"Idioma", L"Language"), 348, 92, 60);
    combo(ID_LANG, 412, 88, 240,
          {S(L"Automático (Windows)", L"Automatic (Windows)"), L"Español", L"English",
           L"Français", L"Deutsch", L"Italiano", L"Nederlands", L"Svenska"});
    label(S(L"Mando", L"Controller"), 668, 92, 60);
    combo(ID_DEVICE, 732, 88, 236, {S(L"Buscando mandos…", L"Searching for controllers…")});

    label(L"Sixaxis", 348, 126, 60);
    combo(ID_MOTION, 412, 122, 240,
          {S(L"Automático: giroscopio si está disponible", L"Automatic: gyro if available"),
           S(L"Giroscopio preferente (respaldo: stick R)", L"Prefer gyroscope (fallback: stick R)"),
           S(L"Stick R únicamente", L"Right stick only")});
    label(S(L"Sensibilidad giro", L"Gyro sensitivity"), 668, 126, 120);
    slider(ID_GYRO_SENS, 792, 120, 176, 20, 300, 100);

    label(S(L"Zona muerta", L"Deadzone"), 348, 160, 84);
    slider(ID_DEADZONE, 436, 154, 216, 2, 50, 15);
    sensor_status = add(L"STATIC", L"", 0, 668, 158, 300, 24);

    // Mapeo interactivo: selector de dispositivo y restablecer
    label(S(L"Mapeo a editar", L"Mapping to edit"), 348, 194, 110);
    combo(ID_MAP_MODE, 468, 190, 270,
          {S(L"Teclado y ratón (simultáneo)", L"Keyboard & Mouse (simultaneous)"),
           S(L"Mando (simultáneo)", L"Controller (simultaneous)")});
    add(L"BUTTON", S(L"Restaurar predeterminados", L"Reset defaults"), ID_DEFAULTS, 754, 190, 214, 26, WS_TABSTOP);

    // Canvas interactivo del mando (628 x 396)
    WNDCLASSW cwc{};
    cwc.lpfnWndProc = canvas_wndproc;
    cwc.hInstance = GetModuleHandleW(nullptr);
    cwc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cwc.lpszClassName = L"bakugan_gamepad_canvas";
    RegisterClassW(&cwc);

    int canvas_y = 226;
    if (canvas_y >= 76)
        canvas_y += 70;
    gamepad_canvas = CreateWindowExW(0, cwc.lpszClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                     344, canvas_y, 628, 396, window,
                                     (HMENU)ID_GAMEPAD_CANVAS, GetModuleHandleW(nullptr), nullptr);
    controls.push_back({gamepad_canvas, 344, canvas_y, 628, 396});

    status = add(L"STATIC",
                 S(L"Haz clic en cualquier botón del mando para asignarlo. Teclado y mando funcionan simultáneamente.",
                   L"Click any button on the controller to bind it. Keyboard and controller work simultaneously."),
                 0, 24, 648, 950, 38);
    add(L"BUTTON", S(L"Guardar ajustes", L"Save settings"), ID_SAVE, 656, 690, 150, 30, WS_TABSTOP);
    add(L"BUTTON", S(L"Jugar", L"Play"), ID_PLAY, 822, 690, 150, 30,
        BS_DEFPUSHBUTTON | WS_TABSTOP);

    fill_controls();
    SetTimer(window, 1, 16, nullptr);
}

void rebuild_controls() {
    if (capture >= 0)
        finish_capture({}, true);
    for (auto &c : controls)
        DestroyWindow(c.hwnd);
    controls.clear();
    create_controls();
    layout();
}

LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hwnd, &paint);
        launcher_theme::paint(hwnd, dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return (LRESULT)launcher_theme::control_color((HWND)lp, (HDC)wp, (HWND)lp == status);
    case WM_DRAWITEM:
        if (wp) {
            launcher_theme::draw_button(*(DRAWITEMSTRUCT *)lp, wp == ID_PLAY);
            return TRUE;
        }
        break;
    case WM_CREATE:
        window = hwnd;
        create_controls();
        return 0;
    case WM_SIZE:
        if (!controls.empty() && wp != SIZE_MINIMIZED)
            layout();
        return 0;
    case WM_DPICHANGED:
        fit_window(HIWORD(wp), (RECT *)lp);
        return 0;
    case WM_TIMER: {
        static ULONGLONG next = 0;
        if (GetTickCount64() >= next) {
            if (capture < 0)
                refresh_devices();
            next = GetTickCount64() + 500;
        } else
            SDL_UpdateGamepads();
        poll_capture();
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == ID_MAP_MODE && HIWORD(wp) == CBN_SELCHANGE)
            update_bindings();
        else if (id == ID_DEVICE && HIWORD(wp) == CBN_SELCHANGE) {
            read_device();
            refresh_devices();
            update_bindings();
        } else if (id == ID_MOTION && HIWORD(wp) == CBN_SELCHANGE) {
            refresh_devices();
            update_bindings();
        } else if (id == ID_UI_LANG && HIWORD(wp) == CBN_SELCHANGE) {
            read_controls();
            PostMessageW(window, WM_APP_REBUILD, 0, 0);
        } else if (id == ID_DEFAULTS) {
            auto d = defaults();
            if (selected(ID_MAP_MODE) == 1)
                config.gamepad = d.gamepad;
            else
                config.keyboard = d.keyboard;
            update_bindings();
            message(S(L"Mapeo predeterminado restaurado. Guarda para conservarlo.",
                      L"Default mapping restored. Save to preserve it."));
        } else if (id == ID_SAVE)
            save_settings();
        else if (id == ID_PLAY)
            launch();
        return 0;
    }
    case WM_APP_REBUILD:
        rebuild_controls();
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        pads.close(true);
        if (font)
            DeleteObject(font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t module[32768];
    GetModuleFileNameW(nullptr, module, 32768);
    directory = std::filesystem::path(module).parent_path().wstring();
    launcher_theme::initialize(directory);
    path = player::config_path();
    std::wstring error;
    player::load(config, path, &error);
    std::string input_error;
    pads.init(input_error);
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES};
    InitCommonControlsEx(&common);
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = instance;
    wc.hIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0,
                                 LR_DEFAULTSIZE | LR_SHARED);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"bakugan_launcher";
    RegisterClassW(&wc);
    HWND hwnd =
        CreateWindowW(wc.lpszClassName, L"Bakugan: Battle Brawlers — Launcher",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                      CW_USEDEFAULT, CW_USEDEFAULT, WIDTH, HEIGHT, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        pads.close(true);
        launcher_theme::shutdown();
        return 1;
    }
    fit_window(GetDpiForWindow(hwnd));
    ShowWindow(hwnd, show);
    if (!error.empty())
        message(localized(error));
    else if (!input_error.empty())
        message(L"SDL3: " + player::utf8_wide(input_error));
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!capture_message(msg) && !activate_button(msg) && !IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    launcher_theme::shutdown();
    return 0;
}
