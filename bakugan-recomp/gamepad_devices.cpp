#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SDL_MAIN_HANDLED
#include <windows.h>
#include <SDL3/SDL_main.h>
#include "gamepad_devices.h"
#include <algorithm>
#include <cmath>
namespace player {
std::wstring utf8_wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L' ');
    if (n)
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
bool Gamepads::init(std::string &error) {
    SDL_SetMainReady();
    /* There is no SDL video window. The owner separately filters host focus. */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        error = SDL_GetError();
        return false;
    }
    initialized = true;
    SDL_SetGamepadEventsEnabled(false);
    refresh();
    return true;
}
void Gamepads::refresh() {
    if (!initialized)
        return;
    SDL_UpdateGamepads();
    for (auto it = items.begin(); it != items.end();) {
        if (!SDL_GamepadConnected(it->handle)) {
            SDL_CloseGamepad(it->handle);
            it = items.erase(it);
        } else
            ++it;
    }
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count; ++i) {
        if (std::any_of(items.begin(), items.end(), [&](const Device &d) { return d.id == ids[i]; }))
            continue;
        SDL_Gamepad *handle = SDL_OpenGamepad(ids[i]);
        if (!handle)
            continue;
        Device d;
        d.id = ids[i];
        d.handle = handle;
        const char *name = SDL_GetGamepadName(handle);
        d.name = name ? name : "Mando";
        const char *serial = SDL_GetGamepadSerial(handle);
        d.serial = serial ? serial : "";
        char guid[64];
        SDL_GUIDToString(SDL_GetGamepadGUIDForID(ids[i]), guid, sizeof guid);
        d.guid = guid;
        d.gyro = SDL_GamepadHasSensor(handle, SDL_SENSOR_GYRO);
        d.accel = SDL_GamepadHasSensor(handle, SDL_SENSOR_ACCEL);
        items.push_back(d);
    }
    SDL_free(ids);
}
Device *Gamepads::selected(const Config &c) {
    if (c.device_guid.empty())
        return items.empty() ? nullptr : &items[0];
    for (auto &d : items)
        if (d.guid == c.device_guid && (c.device_serial.empty() || d.serial == c.device_serial))
            return &d;
    return nullptr; // A different controller must not silently receive this mapping.
}
bool Gamepads::motion(Device *d, std::array<float, 3> &a, std::array<float, 3> &g) {
    if (!d || !d->gyro || !d->accel)
        return false;
    if (!d->sensors_enabled) {
        bool accel = SDL_SetGamepadSensorEnabled(d->handle, SDL_SENSOR_ACCEL, true);
        bool gyro = SDL_SetGamepadSensorEnabled(d->handle, SDL_SENSOR_GYRO, true);
        d->sensors_enabled = accel && gyro;
        if (!d->sensors_enabled)
            return false;
    }
    return SDL_GetGamepadSensorData(d->handle, SDL_SENSOR_ACCEL, a.data(), 3) &&
           SDL_GetGamepadSensorData(d->handle, SDL_SENSOR_GYRO, g.data(), 3);
}
void Gamepads::close(bool quit_subsystem) {
    for (auto &d : items)
        SDL_CloseGamepad(d.handle);
    items.clear();
    if (initialized && quit_subsystem)
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    initialized = false;
}
float binding_strength(Binding b, SDL_Gamepad *pad, bool keyboard, float deadzone) {
    if (b.kind == BindingKind::Key)
        return keyboard && (GetAsyncKeyState(b.code) & 0x8000) ? 1.f : 0.f;
    if (b.kind == BindingKind::Mouse) {
        const int vk[] = {0, VK_LBUTTON, VK_MBUTTON, VK_RBUTTON, VK_XBUTTON1, VK_XBUTTON2};
        return keyboard && b.code > 0 && b.code < 6 && (GetAsyncKeyState(vk[b.code]) & 0x8000) ? 1.f : 0.f;
    }
    if (!pad)
        return 0;
    if (b.kind == BindingKind::Button)
        return SDL_GetGamepadButton(pad, (SDL_GamepadButton)b.code) ? 1.f : 0.f;
    if (b.kind != BindingKind::AxisPositive && b.kind != BindingKind::AxisNegative)
        return 0;
    int raw = SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)b.code);
    float value = b.kind == BindingKind::AxisNegative ? -(float)raw / 32768.f : (float)raw / 32767.f;
    return std::clamp((value - deadzone) / (1.f - deadzone), 0.f, 1.f);
}
} // namespace player
