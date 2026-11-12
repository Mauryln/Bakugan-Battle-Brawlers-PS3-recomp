#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <process.h>
#include "input_bridge.h"
#include "gamepad_devices.h"
#include "input_model.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>
namespace {
// Exported by the toolkit's cellGcmSys.c; the port links against it.
extern "C" unsigned cellGcm_flip_request_count(void);
SRWLOCK lock = SRWLOCK_INIT;
HANDLE worker = nullptr, stop_event = nullptr;
player::Config config;
player::Gamepads pads;
constexpr int MAX_PORTS = 7; // matches CELL_PAD_MAX_PORT_NUM
struct PortState {
    bool connected = false;
    bool is_gamepad = false;
    ps3_host_pad_report report{};
    player::MotionPulse pulse{};
    float filtered_gyro_x = 0.0f;
    float filtered_gyro_y = 0.0f;
};
std::array<PortState, MAX_PORTS> ports{};
bool installed = false;
int provider(u32 port, ps3_host_pad_report *out, void *) {
    if (port >= MAX_PORTS)
        return 0;
    AcquireSRWLockShared(&lock);
    const bool ok = ports[port].connected;
    if (ok)
        *out = ports[port].report;
    ReleaseSRWLockShared(&lock);
    return ok ? 1 : 0;
}
bool focused(HWND &hwnd) {
    hwnd = GetForegroundWindow();
    DWORD pid = 0;
    return hwnd && !IsIconic(hwnd) && GetWindowThreadProcessId(hwnd, &pid) &&
           pid == GetCurrentProcessId();
}

/* MOUSE_SCRIPT=<path>: autonomous gameplay input. The thread polls for the
 * file and, once it appears, replays it on the flip clock so a run is as
 * deterministic as PAD_REPLAY. Each segment holds its delta/buttons until the
 * next one; flips are relative to the moment the script is loaded.
 *
 *   # flip dx dy mouseButtons padMask
 *   flip   frames from script start (cellGcm_flip_request_count)
 *   dx,dy  mouse delta per input tick (pixels)
 *   mouseButtons  1=left, 2=right, 4=middle
 *   padMask       CELL_PAD_CTRL_* bits, OR-ed into the report
 *
 * The virtual deltas feed the same mouse->stick conversion as the real
 * cursor, so a scripted run drives the character exactly like a hand. */
struct VirtualMouse {
    bool loaded = false, active = false;
    unsigned flip0 = 0;
    struct Seg { unsigned flip; int dx, dy; int buttons; unsigned pad; };
    std::vector<Seg> segs;
    size_t cur = 0;
};

static bool load_mouse_script(VirtualMouse &vm) {
    if (vm.loaded) return vm.active;
    const char *path = getenv("MOUSE_SCRIPT");
    if (!path || !*path) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        unsigned flip; int dx, dy, buttons; unsigned pad = 0;
        int n = sscanf(line, "%u %d %d %d %x", &flip, &dx, &dy, &buttons, &pad);
        if (n >= 4) vm.segs.push_back({flip, dx, dy, buttons, pad});
    }
    fclose(f);
    vm.loaded = true;
    if (vm.segs.empty()) return false;
    vm.active = true;
    vm.flip0 = cellGcm_flip_request_count();
    fprintf(stderr, "[input] MOUSE_SCRIPT: %zu segment(s) from %s\n", vm.segs.size(), path);
    return true;
}

static void tick_mouse_script(VirtualMouse &vm) {
    if (!vm.active) return;
    unsigned key = cellGcm_flip_request_count() - vm.flip0;
    while (vm.cur + 1 < vm.segs.size() && vm.segs[vm.cur + 1].flip <= key)
        vm.cur++;
}

void update_mouse(HWND hwnd, std::array<float, player::ACTION_COUNT> &v,
                  HWND &previous_window, POINT &previous_cursor,
                  float &sixaxis_x, float &sixaxis_y,
                  int mouse_buttons, const VirtualMouse &vm) {
    int raw_dx = 0, raw_dy = 0;
    if (vm.active) {
        raw_dx = vm.segs[vm.cur].dx;
        raw_dy = vm.segs[vm.cur].dy;
        previous_window = nullptr;
    } else {
        POINT cursor{};
        const bool cursor_ok = GetCursorPos(&cursor);
        POINT client_cursor = cursor;
        RECT client;
        if (!cursor_ok || !ScreenToClient(hwnd, &client_cursor) || !GetClientRect(hwnd, &client) ||
            !PtInRect(&client, client_cursor)) {
            previous_window = nullptr;
            sixaxis_x *= 0.75f;
            sixaxis_y *= 0.75f;
            if (std::abs(sixaxis_x) < 0.005f) sixaxis_x = 0.0f;
            if (std::abs(sixaxis_y) < 0.005f) sixaxis_y = 0.0f;
            return;
        }
        // Start a new sample when the pointer enters or focus changes.
        if (previous_window == hwnd) {
            raw_dx = cursor.x - previous_cursor.x;
            raw_dy = cursor.y - previous_cursor.y;
        }
        previous_window = hwnd;
        previous_cursor = cursor;
    }

    bool held = config.mouse_look && (mouse_buttons & 2);
    float sens = (config.mouse_sensitivity > 0.01f ? config.mouse_sensitivity : 0.5f);

    if (held) {
        // Right click held: mouse look / camera aim (drives Left stick)
        float x = std::clamp(raw_dx * sens / 30.f, -1.f, 1.f);
        float y = std::clamp(raw_dy * sens / 30.f, -1.f, 1.f);
        v[player::L_LEFT] = std::max(v[player::L_LEFT], std::max(0.f, -x));
        v[player::L_RIGHT] = std::max(v[player::L_RIGHT], std::max(0.f, x));
        v[player::L_UP] = std::max(v[player::L_UP], std::max(0.f, -y));
        v[player::L_DOWN] = std::max(v[player::L_DOWN], std::max(0.f, y));

        sixaxis_x *= 0.80f;
        sixaxis_y *= 0.80f;
    } else {
        // Free mouse movement: simulate Sixaxis roll and pitch
        if (std::abs(raw_dx) <= 1) raw_dx = 0;
        if (std::abs(raw_dy) <= 1) raw_dy = 0;

        float target_x = std::clamp((raw_dx * sens) / 25.0f, -1.0f, 1.0f);
        float target_y = std::clamp((-raw_dy * sens) / 25.0f, -1.0f, 1.0f); // Mouse up = tilt forward/up

        constexpr float kAttack = 0.50f;
        constexpr float kDecay = 0.80f;

        if (std::abs(target_x) > 0.01f)
            sixaxis_x = sixaxis_x * (1.0f - kAttack) + target_x * kAttack;
        else
            sixaxis_x *= kDecay;

        if (std::abs(target_y) > 0.01f)
            sixaxis_y = sixaxis_y * (1.0f - kAttack) + target_y * kAttack;
        else
            sixaxis_y *= kDecay;

        if (std::abs(sixaxis_x) < 0.005f) sixaxis_x = 0.0f;
        if (std::abs(sixaxis_y) < 0.005f) sixaxis_y = 0.0f;

        sixaxis_x = std::clamp(sixaxis_x, -1.0f, 1.0f);
        sixaxis_y = std::clamp(sixaxis_y, -1.0f, 1.0f);
    }

    // Apply mouse Sixaxis deflection to right-stick virtual channels
    if (sixaxis_x > 0.0f)
        v[player::R_RIGHT] = std::max(v[player::R_RIGHT], sixaxis_x);
    else if (sixaxis_x < 0.0f)
        v[player::R_LEFT] = std::max(v[player::R_LEFT], -sixaxis_x);

    if (sixaxis_y > 0.0f)
        v[player::R_UP] = std::max(v[player::R_UP], sixaxis_y);
    else if (sixaxis_y < 0.0f)
        v[player::R_DOWN] = std::max(v[player::R_DOWN], -sixaxis_y);
}
ps3_host_pad_report neutral_report() {
    ps3_host_pad_report r{};
    r.lx = r.ly = r.rx = r.ry = 128;
    r.sensors[0] = 512;
    r.sensors[1] = 399;
    r.sensors[2] = r.sensors[3] = 512;
    r.sensors_valid = 1;
    return r;
}
ps3_host_pad_report to_host_report(const player::Report &r) {
    ps3_host_pad_report out{};
    out.buttons = r.buttons;
    out.lx = r.lx;
    out.ly = r.ly;
    out.rx = r.rx;
    out.ry = r.ry;
    out.l2 = r.l2;
    out.r2 = r.r2;
    out.sensors_valid = 1;
    for (int i = 0; i < 4; ++i)
        out.sensors[i] = r.sensors[i];
    return out;
}
ps3_host_pad_report sample_gamepad(player::Device *device, bool foreground, ULONGLONG now,
                                   PortState &state) {
    std::array<float, player::ACTION_COUNT> gp_values{};
    std::array<float, 3> a{}, g{};
    SDL_Gamepad *pad = device ? device->handle : nullptr;
    bool real = foreground && pad && config.motion != player::MotionMode::RightStick &&
                pads.motion(device, a, g);
    if (foreground && pad) {
        player::read_gamepad_actions(config, pad, gp_values);
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
            state.filtered_gyro_x = state.filtered_gyro_x * kAlpha + raw_yaw * (1.0f - kAlpha);
            state.filtered_gyro_y = state.filtered_gyro_y * kAlpha + raw_pitch * (1.0f - kAlpha);
            float sens = config.gyro_sensitivity > 0.01f ? config.gyro_sensitivity : 1.0f;
            float sign_x = config.gyro_invert_x ? 1.0f : -1.0f;
            float sign_y = config.gyro_invert_y ? -1.0f : 1.0f;
            float gyro_deflection_x = std::clamp((state.filtered_gyro_x * sign_x * sens) / 2.0f, -1.0f, 1.0f);
            float gyro_deflection_y = std::clamp((state.filtered_gyro_y * sign_y * sens) / 2.0f, -1.0f, 1.0f);

            float phys_rx = gp_values[player::R_RIGHT] - gp_values[player::R_LEFT];
            float phys_ry = gp_values[player::R_DOWN] - gp_values[player::R_UP];
            float total_rx = std::clamp(phys_rx + gyro_deflection_x, -1.0f, 1.0f);
            float total_ry = std::clamp(phys_ry + gyro_deflection_y, -1.0f, 1.0f);
            gp_values[player::R_LEFT] = std::max(0.0f, -total_rx);
            gp_values[player::R_RIGHT] = std::max(0.0f, total_rx);
            gp_values[player::R_UP] = std::max(0.0f, -total_ry);
            gp_values[player::R_DOWN] = std::max(0.0f, total_ry);
        } else {
            state.filtered_gyro_x = 0.0f;
            state.filtered_gyro_y = 0.0f;
        }
    } else {
        state.pulse = {};
        state.filtered_gyro_x = 0.0f;
        state.filtered_gyro_y = 0.0f;
    }
    return to_host_report(player::make_report(gp_values, config.motion, real, a, g, now, state.pulse));
}
ps3_host_pad_report sample_keyboard(bool foreground, HWND hwnd, POINT &previous_cursor,
                                    HWND &previous_mouse_window, float &mouse_sixaxis_x,
                                    float &mouse_sixaxis_y, const VirtualMouse &vm,
                                    ULONGLONG now, player::MotionPulse &pulse) {
    std::array<float, player::ACTION_COUNT> kb_values{};
    if (foreground) {
        for (int i = 0; i < player::ACTION_COUNT; ++i)
            kb_values[i] = player::binding_strength(config.keyboard[i], nullptr, true, config.deadzone);
        int mouse_buttons = ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0) |
                            ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) ? 2 : 0) |
                            ((GetAsyncKeyState(VK_MBUTTON) & 0x8000) ? 4 : 0);
        if (vm.active) mouse_buttons = vm.segs[vm.cur].buttons;
        if (config.mouse_left_cross && (mouse_buttons & 1))
            kb_values[player::CROSS] = 1.0f;
        update_mouse(hwnd, kb_values, previous_mouse_window, previous_cursor,
                     mouse_sixaxis_x, mouse_sixaxis_y, mouse_buttons, vm);
    } else {
        previous_mouse_window = nullptr;
        mouse_sixaxis_x = 0.0f;
        mouse_sixaxis_y = 0.0f;
        pulse = {};
    }
    std::array<float, 3> a{}, g{};
    ps3_host_pad_report result = to_host_report(
        player::make_report(kb_values, config.motion, false, a, g, now, pulse));
    if (foreground && vm.active)
        result.buttons |= (uint16_t)vm.segs[vm.cur].pad;
    return result;
}
unsigned __stdcall run(void *) {
    player::MotionPulse kb_pulse;
    HWND previous_mouse_window = nullptr;
    POINT previous_cursor{};
    float mouse_sixaxis_x = 0.0f;
    float mouse_sixaxis_y = 0.0f;
    ULONGLONG refresh = 0;
    ULONGLONG script_poll = 0;
    VirtualMouse vm;
    std::string prior;
    while (WaitForSingleObject(stop_event, 8) == WAIT_TIMEOUT) {
        ULONGLONG now = GetTickCount64();
        if (now >= refresh) {
            pads.refresh();
            refresh = now + 500;
        } else
            SDL_UpdateGamepads();
        // The script file is written by the launcher once the route replay is
        // spent; polling keeps the guest timeline anchored to that moment.
        if (now >= script_poll) {
            load_mouse_script(vm);
            script_poll = now + 120;
        }
        tick_mouse_script(vm);
        bool allow_keyboard = (config.input == player::InputMode::Auto ||
                               config.input == player::InputMode::KeyboardMouse);
        bool allow_gamepad = (config.input == player::InputMode::Auto ||
                              config.input == player::InputMode::Gamepad);
        std::vector<player::Device *> ordered;
        if (allow_gamepad) {
            player::Device *preferred = pads.selected(config);
            if (preferred)
                ordered.push_back(preferred);
            for (size_t i = 0; i < pads.devices().size(); ++i) {
                player::Device *d = pads.at(i);
                if (d && d != preferred)
                    ordered.push_back(d);
            }
        }
        HWND hwnd = nullptr;
        bool foreground = focused(hwnd);
        std::array<PortState, MAX_PORTS> next_ports{};
        int next_idx = 0;
        std::string state;
        // VirtualMouse scripts always drive port 0 alongside whatever controller is there.
        if (vm.active && allow_keyboard) {
            ps3_host_pad_report kb = sample_keyboard(foreground, hwnd, previous_cursor,
                previous_mouse_window, mouse_sixaxis_x, mouse_sixaxis_y, vm, now, kb_pulse);
            next_ports[0].connected = true;
            next_ports[0].report = kb;
            next_idx = 1;
            state = "P1: keyboard/mouse (script)";
        } else {
            for (size_t i = 0; i < ordered.size() && next_idx < MAX_PORTS; ++i) {
                if (ports[next_idx].is_gamepad)
                    next_ports[next_idx] = ports[next_idx];
                next_ports[next_idx].connected = true;
                next_ports[next_idx].is_gamepad = true;
                next_ports[next_idx].report = sample_gamepad(ordered[i], foreground, now, next_ports[next_idx]);
                if (!state.empty()) state += " | ";
                state += "P" + std::to_string(next_idx + 1) + ": " + ordered[i]->name;
                ++next_idx;
            }
            if (allow_keyboard && next_idx < MAX_PORTS) {
                next_ports[next_idx].connected = true;
                next_ports[next_idx].is_gamepad = false;
                next_ports[next_idx].report = sample_keyboard(foreground, hwnd, previous_cursor,
                    previous_mouse_window, mouse_sixaxis_x, mouse_sixaxis_y, vm, now, kb_pulse);
                if (!state.empty()) state += " | ";
                state += "P" + std::to_string(next_idx + 1) + ": keyboard/mouse";
                ++next_idx;
            }
        }
        for (int i = next_idx; i < MAX_PORTS; ++i) {
            next_ports[i] = {};
            next_ports[i].report = neutral_report();
        }
        AcquireSRWLockExclusive(&lock);
        for (int i = 0; i < MAX_PORTS; ++i)
            ports[i] = next_ports[i];
        ReleaseSRWLockExclusive(&lock);
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
    wchar_t path[32768];
    DWORD n = GetEnvironmentVariableW(L"PS3_PLAYER_CONFIG", path, 32768);
    if (!n) {
        // Keep deterministic PAD_REPLAY runs isolated from physical pads on the host.
        const char *replay = getenv("PAD_REPLAY");
        const char *legacy = getenv("PS3_LEGACY_INPUT");
        if ((replay && *replay && !getenv("MOUSE_SCRIPT")) ||
            (legacy && *legacy && std::strcmp(legacy, "0") != 0))
            return true;
        std::wstring cfg_path = player::config_path();
        if (!cfg_path.empty())
            player::load(config, cfg_path, nullptr);
        else
            config = player::defaults();
    } else if (n >= 32768) {
        return false;
    } else {
        std::wstring error;
        if (!player::load(config, path, &error)) {
            fprintf(stderr, "[input] config failed\n");
            return false;
        }
    }
    std::string sdl_error;
    if (!pads.init(sdl_error)) {
        fprintf(stderr, "[input] SDL3 init failed: %s\n", sdl_error.c_str());
        return false;
    }
    for (int i = 0; i < MAX_PORTS; ++i) {
        ports[i] = {};
        ports[i].report = neutral_report();
    }
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
    *out = (ports[0].connected && ports[0].is_gamepad) ? ports[0].report : ps3_host_pad_report{};
    ReleaseSRWLockShared(&lock);
    return true;
}
