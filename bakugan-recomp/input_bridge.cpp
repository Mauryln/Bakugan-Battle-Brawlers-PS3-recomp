#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <process.h>
#include "input_bridge.h"
#include "gamepad_devices.h"
#include "input_model.h"
#include <cstdio>
#include <cstring>
namespace {
SRWLOCK lock = SRWLOCK_INIT;
HANDLE worker = nullptr, stop_event = nullptr;
player::Config config;
player::Gamepads pads;
ps3_host_pad_report latest{};
bool installed = false, latest_gamepad = false;
int provider(u32 port, ps3_host_pad_report *out, void *) {
    if (port != 0)
        return 0;
    AcquireSRWLockShared(&lock);
    *out = latest;
    ReleaseSRWLockShared(&lock);
    return 1;
}
bool focused(HWND &hwnd) {
    hwnd = GetForegroundWindow();
    DWORD pid = 0;
    return hwnd && GetWindowThreadProcessId(hwnd, &pid) && pid == GetCurrentProcessId();
}
void mouse_look(HWND hwnd, std::array<float, player::ACTION_COUNT> &v, bool &engaged) {
    bool held = config.mouse_look && (GetAsyncKeyState(VK_RBUTTON) & 0x8000);
    POINT cursor;
    RECT client;
    if (!held || !GetCursorPos(&cursor) || !ScreenToClient(hwnd, &cursor) || !GetClientRect(hwnd, &client) ||
        !PtInRect(&client, cursor)) {
        engaged = false;
        return;
    }
    POINT center{(client.right - client.left) / 2, (client.bottom - client.top) / 2};
    int dx = cursor.x - center.x, dy = cursor.y - center.y;
    if (engaged) {
        float x = std::clamp(dx * config.mouse_sensitivity / 30.f, -1.f, 1.f),
              y = std::clamp(dy * config.mouse_sensitivity / 30.f, -1.f, 1.f);
        v[player::L_LEFT] = std::max(0.f, -x);
        v[player::L_RIGHT] = std::max(0.f, x);
        v[player::L_UP] = std::max(0.f, -y);
        v[player::L_DOWN] = std::max(0.f, y);
    }
    HWND still;
    if (!focused(still) || still != hwnd) {
        engaged = false;
        return;
    }
    if (ClientToScreen(hwnd, &center))
        SetCursorPos(center.x, center.y);
    engaged = true;
}
unsigned __stdcall run(void *) {
    player::MotionPulse pulse;
    bool mouse_engaged = false;
    ULONGLONG refresh = 0;
    std::string prior;
    float filtered_gyro_x = 0.0f;
    float filtered_gyro_y = 0.0f;
    while (WaitForSingleObject(stop_event, 8) == WAIT_TIMEOUT) {
        ULONGLONG now = GetTickCount64();
        if (now >= refresh) {
            pads.refresh();
            refresh = now + 500;
        } else
            SDL_UpdateGamepads();
        player::Device *device = pads.selected(config);
        bool allow_keyboard = (config.input == player::InputMode::Auto ||
                               config.input == player::InputMode::KeyboardMouse);
        bool allow_gamepad = (config.input == player::InputMode::Auto ||
                              config.input == player::InputMode::Gamepad);
        SDL_Gamepad *pad = (allow_gamepad && device) ? device->handle : nullptr;
        std::array<float, player::ACTION_COUNT> values{};
        std::array<float, 3> a{}, g{};
        bool real = pad && config.motion != player::MotionMode::RightStick && pads.motion(device, a, g);
        HWND hwnd;
        bool foreground = focused(hwnd);
        if (foreground) {
            std::array<float, player::ACTION_COUNT> kb_values{};
            std::array<float, player::ACTION_COUNT> gp_values{};
            if (allow_keyboard) {
                for (int i = 0; i < player::ACTION_COUNT; ++i)
                    kb_values[i] = player::binding_strength(config.keyboard[i], nullptr, true, config.deadzone);
                if (config.mouse_left_cross && (GetAsyncKeyState(VK_LBUTTON) & 0x8000))
                    kb_values[player::CROSS] = 1.0f;
                mouse_look(hwnd, kb_values, mouse_engaged);
            }
            if (allow_gamepad && pad) {
                for (int i = 0; i < player::ACTION_COUNT; ++i)
                    gp_values[i] = player::binding_strength(config.gamepad[i], pad, false, config.deadzone);
            }
            if (real) {
                float raw_yaw = g[1];
                float raw_pitch = g[0];
                float speed = std::sqrt(raw_yaw * raw_yaw + raw_pitch * raw_pitch);
                float gz = config.gyro_deadzone > 0.001f ? config.gyro_deadzone : 0.05f;
                if (speed < gz) {
                    raw_yaw = 0.0f;
                    raw_pitch = 0.0f;
                } else {
                    float factor = (speed - gz) / speed;
                    raw_yaw *= factor;
                    raw_pitch *= factor;
                }
                constexpr float kAlpha = 0.35f;
                filtered_gyro_x = filtered_gyro_x * kAlpha + raw_yaw * (1.0f - kAlpha);
                filtered_gyro_y = filtered_gyro_y * kAlpha + raw_pitch * (1.0f - kAlpha);
                float sens = config.gyro_sensitivity > 0.01f ? config.gyro_sensitivity : 1.0f;
                float sign_x = config.gyro_invert_x ? 1.0f : -1.0f;
                float sign_y = config.gyro_invert_y ? -1.0f : 1.0f;
                float gyro_deflection_x = std::clamp((filtered_gyro_x * sign_x * sens) / 2.0f, -1.0f, 1.0f);
                float gyro_deflection_y = std::clamp((filtered_gyro_y * sign_y * sens) / 2.0f, -1.0f, 1.0f);

                float phys_rx = gp_values[player::R_RIGHT] - gp_values[player::R_LEFT];
                float phys_ry = gp_values[player::R_DOWN] - gp_values[player::R_UP];
                float total_rx = std::clamp(phys_rx + gyro_deflection_x, -1.0f, 1.0f);
                float total_ry = std::clamp(phys_ry + gyro_deflection_y, -1.0f, 1.0f);
                gp_values[player::R_LEFT] = std::max(0.0f, -total_rx);
                gp_values[player::R_RIGHT] = std::max(0.0f, total_rx);
                gp_values[player::R_UP] = std::max(0.0f, -total_ry);
                gp_values[player::R_DOWN] = std::max(0.0f, total_ry);
            } else {
                filtered_gyro_x = 0.0f;
                filtered_gyro_y = 0.0f;
            }
            for (int i = 0; i < player::ACTION_COUNT; ++i)
                values[i] = std::max(kb_values[i], gp_values[i]);
        } else {
            mouse_engaged = false;
            real = false;
            pulse = {};
            filtered_gyro_x = 0.0f;
            filtered_gyro_y = 0.0f;
        }
        auto report = player::make_report(values, config.motion, real, a, g, now, pulse);
        ps3_host_pad_report result{};
        result.buttons = report.buttons;
        result.lx = report.lx;
        result.ly = report.ly;
        result.rx = report.rx;
        result.ry = report.ry;
        result.l2 = report.l2;
        result.r2 = report.r2;
        result.sensors_valid = 1;
        for (int i = 0; i < 4; ++i)
            result.sensors[i] = report.sensors[i];
        AcquireSRWLockExclusive(&lock);
        latest = result;
        latest_gamepad = (pad != nullptr);
        ReleaseSRWLockExclusive(&lock);
        std::string state;
        if (allow_keyboard && pad)
            state = "simultaneous (" + device->name + " + keyboard/mouse)";
        else if (pad)
            state = device->name;
        else
            state = "keyboard/mouse";
        state += real ? " | gyro active -> virtual right stick + Sixaxis" : " | right-stick Sixaxis";
        if (state != prior) {
            fprintf(stderr, "[input] %s\n", state.c_str());
            prior = state;
        }
    }
    pads.close(false);
    return 0;
}
} // namespace
bool bakugan_input_install() {
    // Direct developer launches retain their existing input path unless configured.
    wchar_t path[32768];
    DWORD n = GetEnvironmentVariableW(L"PS3_PLAYER_CONFIG", path, 32768);
    if (!n)
        return true;
    if (n >= 32768)
        return false;
    std::wstring error;
    if (!player::load(config, path, &error)) {
        fprintf(stderr, "[input] config failed\n");
        return false;
    }
    std::string sdl_error;
    if (!pads.init(sdl_error)) {
        fprintf(stderr, "[input] SDL3 init failed: %s\n", sdl_error.c_str());
        return false;
    }
    latest.lx = latest.ly = latest.rx = latest.ry = 128;
    latest.sensors[0] = 512;
    latest.sensors[1] = 399;
    latest.sensors[2] = latest.sensors[3] = 512;
    latest.sensors_valid = 1;
    stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event) {
        pads.close(true);
        return false;
    }
    worker = (HANDLE)_beginthreadex(nullptr, 0, run, nullptr, 0, nullptr);
    if (!worker) {
        CloseHandle(stop_event);
        stop_event = nullptr;
        pads.close(true);
        return false;
    }
    installed = true;
    ps3_pad_set_host_provider(provider, nullptr);
    return true;
}
void bakugan_input_shutdown() {
    if (!installed)
        return;
    SetEvent(stop_event);
    WaitForSingleObject(worker, INFINITE);
    CloseHandle(worker);
    CloseHandle(stop_event);
    worker = stop_event = nullptr;
    installed = false;
}
bool bakugan_input_gamepad_snapshot(ps3_host_pad_report *out) {
    if (!installed)
        return false;
    AcquireSRWLockShared(&lock);
    *out = latest_gamepad ? latest : ps3_host_pad_report{};
    ReleaseSRWLockShared(&lock);
    return true;
}
