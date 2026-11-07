#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
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
    ID_CAP,
    ID_WINDOW,
    ID_FSR,
    ID_SHARP,
    ID_ANISO,
    ID_LANG,
    ID_MODE,
    ID_DEVICE,
    ID_MOTION,
    ID_DEADZONE,
    ID_SENSITIVITY,
    ID_MOUSE_LOOK,
    ID_MOUSE_CROSS,
    ID_MAP_MODE,
    ID_DEFAULTS,
    ID_SAVE,
    ID_PLAY
};
constexpr int ID_BIND = 200;
constexpr int WIDTH = 1000, HEIGHT = 800;
struct Control {
    HWND hwnd;
    int x, y, w, h;
};
std::vector<Control> controls;
HWND window, status, sensor_status, bindings[ACTION_COUNT];
HFONT font = nullptr;
Config config;
Gamepads pads;
std::wstring path, directory;
std::string device_signature;
int capture = -1;
bool capture_armed = false;
ULONGLONG capture_start = 0;
const wchar_t *outputs[] = {L"720p", L"1080p", L"1440p", L"2160p", L"auto", L"native"};
const wchar_t *caps[] = {L"30", L"60", L"90", L"120", L"monitor", L"0"};
const wchar_t *windows[] = {L"borderless", L"windowed"};
const wchar_t *fsrs[] = {L"auto", L"fsr1", L"0"};
const wchar_t *languages[] = {L"auto", L"es", L"en", L"fr", L"de", L"it", L"nl", L"sv"};
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
    const auto &map = selected(ID_MAP_MODE) == 1 ? config.gamepad : config.keyboard;
    for (int i = 0; i < ACTION_COUNT; ++i)
        SetWindowTextW(bindings[i], binding_name(map[i]).c_str());
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
        SendMessageW(box, CB_ADDSTRING, 0, (LPARAM)L"Automático: primer mando conectado");
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
            SendMessageW(box, CB_ADDSTRING, 0, (LPARAM)L"Mando guardado (desconectado)");
        }
        choose(ID_DEVICE, index);
    }
    Device *device = pads.selected(config);
    std::wstring info = L"Sin mando conectado. Sixaxis: palanca R / teclas I J K L.";
    if (device) {
        std::array<float, 3> a{}, g{};
        bool real = selected(ID_MOTION) != 2 && pads.motion(device, a, g);
        info = real ? L"Giroscopio + acelerómetro disponibles: Sixaxis real."
                    : L"Sensores no disponibles o desactivados: Sixaxis en palanca R.";
    }
    SetWindowTextW(sensor_status, info.c_str());
}
void read_controls() {
    auto index = [](int id, int count) { return std::clamp(selected(id), 0, count - 1); };
    config.output = outputs[index(ID_OUTPUT, 6)];
    config.cap = caps[index(ID_CAP, 6)];
    config.window = windows[index(ID_WINDOW, 2)];
    config.fsr = fsrs[index(ID_FSR, 3)];
    config.sharpness = position(ID_SHARP) / 100.f;
    const int aniso[] = {1, 2, 4, 8, 16};
    config.aniso = aniso[index(ID_ANISO, 5)];
    config.language = languages[index(ID_LANG, 8)];
    config.input = (InputMode)index(ID_MODE, 3);
    config.motion = (MotionMode)index(ID_MOTION, 3);
    config.deadzone = position(ID_DEADZONE) / 100.f;
    config.mouse_sensitivity = position(ID_SENSITIVITY) / 100.f;
    config.mouse_look = checked(ID_MOUSE_LOOK);
    config.mouse_left_cross = checked(ID_MOUSE_CROSS);
    read_device();
}
void fill_controls() {
    choose(ID_OUTPUT, outputs, config.output);
    choose(ID_CAP, caps, config.cap);
    choose(ID_WINDOW, windows, config.window);
    choose(ID_FSR, fsrs, config.fsr);
    choose(ID_LANG, languages, config.language);
    choose(ID_MODE, (int)config.input);
    choose(ID_MOTION, (int)config.motion);
    int a = 0;
    for (int n = config.aniso; n > 1; n /= 2)
        ++a;
    choose(ID_ANISO, a);
    SendMessageW(GetDlgItem(window, ID_SHARP), TBM_SETPOS, TRUE, (int)(config.sharpness * 100));
    SendMessageW(GetDlgItem(window, ID_DEADZONE), TBM_SETPOS, TRUE, (int)(config.deadzone * 100));
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
        message(error);
        return false;
    }
    message(L"Ajustes guardados. Se aplicarán al iniciar el juego.");
    return true;
}
void launch() {
    if (!save_settings())
        return;
    std::filesystem::path exe = std::filesystem::path(directory) / L"BakuganRecomp.exe";
    if (!std::filesystem::exists(exe)) {
        message(L"No se encuentra BakuganRecomp.exe junto al launcher.");
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
        dialog.lpstrFilter = L"Ejecutable del juego (EBOOT.elf)\0EBOOT.elf\0";
        dialog.lpstrFile = file;
        dialog.nMaxFile = 32768;
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&dialog)) {
            message(L"Selecciona EBOOT.elf para iniciar.");
            return;
        }
        elf = file;
    }
    elf = std::filesystem::absolute(elf).lexically_normal();
    if (elf.parent_path().filename() != L"USRDIR" ||
        elf.parent_path().parent_path().filename() != L"PS3_GAME") {
        message(L"EBOOT.elf debe estar dentro de PS3_GAME/USRDIR.");
        return;
    }
    apply_environment(config, path);
    SetEnvironmentVariableW(L"PS3_VFS_ROOT", elf.parent_path().parent_path().parent_path().c_str());
    SetEnvironmentVariableW(L"PS3_TITLE", L"Bakugan: Battle Brawlers");
    std::wstring command = L"\"" + exe.wstring() + L"\" \"" + elf.wstring() + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(),
                        &startup, &process)) {
        message(L"No se pudo iniciar el juego. Error " + std::to_wstring(GetLastError()));
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    ShowWindow(window, SW_MINIMIZE);
    message(L"Juego iniciado. Puedes cerrar el launcher.");
}
void finish_capture(Binding binding, bool cancel = false) {
    if (!cancel) {
        auto &map = selected(ID_MAP_MODE) == 1 ? config.gamepad : config.keyboard;
        map[capture] = binding;
    }
    capture = -1;
    capture_armed = false;
    update_bindings();
    message(cancel ? L"Asignación cancelada."
                   : L"Asignación aplicada. Guarda los ajustes o inicia el juego.");
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
void create_controls() {
    add(L"BUTTON", L"Pantalla y gráficos", 0, 16, 76, 302, 558, BS_GROUPBOX);
    label(L"Resolución de salida", 32, 100);
    combo(ID_OUTPUT, 32, 124, 268,
          {L"1280 × 720", L"1920 × 1080", L"2560 × 1440", L"3840 × 2160", L"Automática",
           L"Resolución del monitor"});
    label(L"Límite de FPS", 32, 158);
    combo(ID_CAP, 32, 182, 268,
          {L"30 FPS (recomendado)", L"60 FPS", L"90 FPS", L"120 FPS", L"Frecuencia del monitor",
           L"Sin límite"});
    label(L"Modo de ventana", 32, 216);
    combo(ID_WINDOW, 32, 240, 268, {L"Sin bordes", L"Ventana"});
    label(L"Reescalado", 32, 274);
    combo(ID_FSR, 32, 298, 268, {L"FSR automático", L"FSR 1", L"Desactivado"});
    label(L"Nitidez FSR: 0 alta · 2 baja", 32, 332);
    slider(ID_SHARP, 28, 356, 276, 0, 200, 60);
    label(L"Filtrado anisotrópico", 32, 394);
    combo(ID_ANISO, 32, 418, 268, {L"1×", L"2×", L"4×", L"8×", L"16×"});
    add(L"STATIC",
        L"La resolución de salida se adapta al monitor. El límite de FPS no garantiza el rendimiento del "
        L"equipo.",
        0, 32, 468, 268, 72);
    add(L"BUTTON", L"Mantener clic derecho: apuntar", ID_MOUSE_LOOK, 32, 550, 276, 26,
        BS_AUTOCHECKBOX | WS_TABSTOP);
    add(L"BUTTON", L"Clic izquierdo: Cruz (X)", ID_MOUSE_CROSS, 32, 584, 276, 26,
        BS_AUTOCHECKBOX | WS_TABSTOP);
    add(L"BUTTON", L"Idioma y controles", 0, 332, 76, 652, 558, BS_GROUPBOX);
    label(L"Idioma", 348, 100, 112);
    combo(ID_LANG, 468, 96, 496,
          {L"Automático (idioma de Windows)", L"Español", L"English", L"Français", L"Deutsch", L"Italiano",
           L"Nederlands", L"Svenska"});
    label(L"Dispositivo", 348, 134, 112);
    combo(ID_MODE, 468, 130, 496, {L"Automático: mando o teclado y ratón", L"Teclado y ratón", L"Mando"});
    label(L"Mando", 348, 168, 112);
    combo(ID_DEVICE, 468, 164, 496, {L"Buscando mandos…"});
    label(L"Sixaxis", 348, 202, 112);
    combo(ID_MOTION, 468, 198, 496,
          {L"Automático: sensores o palanca R", L"Preferir giroscopio (respaldo: palanca R)", L"Palanca R"});
    sensor_status = add(L"STATIC", L"", 0, 348, 234, 616, 24);
    label(L"Zona muerta", 348, 270, 112);
    slider(ID_DEADZONE, 458, 264, 190, 2, 50, 15);
    label(L"Ratón", 670, 270, 64);
    slider(ID_SENSITIVITY, 734, 264, 230, 5, 200, 50);
    label(L"Mapeo PS3", 348, 304, 112);
    combo(ID_MAP_MODE, 468, 298, 280, {L"Editar teclado y ratón", L"Editar mando"});
    add(L"BUTTON", L"Restablecer", ID_DEFAULTS, 770, 298, 194, 26, WS_TABSTOP);
    for (int i = 0; i < ACTION_COUNT; ++i) {
        int column = i / 13, row = i % 13, x = 348 + column * 312, y = 338 + row * 22;
        label(action_name(i), x, y + 2, 142);
        bindings[i] = add(L"BUTTON", L"", ID_BIND + i, x + 144, y, 152, 22, WS_TABSTOP);
    }
    status = add(L"STATIC",
                 L"Selecciona un botón para asignarlo. Suelta los controles y pulsa la nueva entrada. Esc "
                 L"cancela; Supr borra.",
                 0, 24, 648, 950, 38);
    add(L"BUTTON", L"Guardar ajustes", ID_SAVE, 656, 690, 150, 30, WS_TABSTOP);
    add(L"BUTTON", L"Jugar", ID_PLAY, 822, 690, 150, 30, BS_DEFPUSHBUTTON | WS_TABSTOP);
    fill_controls();
    SetTimer(window, 1, 16, nullptr);
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
        if (capture >= 0) {
            if (id == ID_BIND + capture)
                return 0;
            finish_capture({}, true);
        }
        if (id >= ID_BIND && id < ID_BIND + ACTION_COUNT) {
            capture = id - ID_BIND;
            capture_start = GetTickCount64();
            capture_armed = false;
            SetWindowTextW(bindings[capture], L"Pulsa una entrada…");
            message(L"Suelta los controles y pulsa una tecla, botón o eje. Esc cancela; Supr borra.");
        } else if (id == ID_MAP_MODE && HIWORD(wp) == CBN_SELCHANGE)
            update_bindings();
        else if (id == ID_DEVICE && HIWORD(wp) == CBN_SELCHANGE) {
            read_device();
            refresh_devices();
        } else if (id == ID_MOTION && HIWORD(wp) == CBN_SELCHANGE)
            refresh_devices();
        else if (id == ID_DEFAULTS) {
            auto d = defaults();
            if (selected(ID_MAP_MODE) == 1)
                config.gamepad = d.gamepad;
            else
                config.keyboard = d.keyboard;
            update_bindings();
            message(L"Mapeo predeterminado restaurado. Guarda para conservarlo.");
        } else if (id == ID_SAVE)
            save_settings();
        else if (id == ID_PLAY)
            launch();
        return 0;
    }
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
        message(error);
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
