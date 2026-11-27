#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <process.h>
#include "input_bridge.h"
#include "gamepad_devices.h"
#include "input_model.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
namespace {
// Exported by the toolkit's cellGcmSys.c; the port links against it.
extern "C" unsigned cellGcm_flip_request_count(void);
extern "C" bool bakugan_savedata_overlay_active();
extern "C" void rsx_d3d12_backend_set_button_prompts(int style);
SRWLOCK lock = SRWLOCK_INIT;
HANDLE worker = nullptr, stop_event = nullptr;
player::Config config;
player::Gamepads pads;
std::atomic<int> active_prompt_style{static_cast<int>(player::ButtonPromptStyle::PlayStation)};
int last_published_prompt_style = -1;
void publish_prompt_style(player::ButtonPromptStyle style) {
    const int next = static_cast<int>(style);
    active_prompt_style.store(next, std::memory_order_relaxed);
    if (next != last_published_prompt_style) {
        last_published_prompt_style = next;
        rsx_d3d12_backend_set_button_prompts(next);
        fprintf(stderr, "[input] button prompts -> %s\n", player::prompt_style_name(style));
    }
}
constexpr int MAX_PORTS = 7; // matches CELL_PAD_MAX_PORT_NUM
constexpr Uint32 RUMBLE_HOLD_MS = 1000;
constexpr ULONGLONG RUMBLE_RENEW_MS = 400;
struct RumbleTarget {
    uint16_t lo = 0;
    uint16_t hi = 0;
    bool dirty = false;
};
SRWLOCK rumble_lock = SRWLOCK_INIT;
std::array<RumbleTarget, MAX_PORTS> rumble_targets{};
struct PortState {
    bool connected = false;
    bool is_gamepad = false;
    SDL_JoystickID device_id = 0;
    ps3_host_pad_report report{};
    player::MotionPulse pulse{};
    player::SixaxisState sixaxis{};
};
std::array<PortState, MAX_PORTS> ports{};
bool installed = false;
int actuator_sink(u32 port, u16 low_freq, u16 high_freq, void *) {
    if (port >= MAX_PORTS)
        return 0;
    AcquireSRWLockExclusive(&rumble_lock);
    auto &t = rumble_targets[port];
    if (t.lo != low_freq || t.hi != high_freq) {
        t.lo = low_freq;
        t.hi = high_freq;
        t.dirty = true;
    }
    ReleaseSRWLockExclusive(&rumble_lock);
    return 1;
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
int provider(u32 port, ps3_host_pad_report *out, void *) {
    if (port >= MAX_PORTS)
        return 0;
    AcquireSRWLockShared(&lock);
    const bool ok = ports[port].connected;
    if (ok)
        *out = bakugan_savedata_overlay_active() ? neutral_report() : ports[port].report;
    ReleaseSRWLockShared(&lock);
    return ok ? 1 : 0;
}
bool focused(HWND &hwnd) {
    hwnd = GetForegroundWindow();
    DWORD pid = 0;
    if (!hwnd || IsIconic(hwnd) || !GetWindowThreadProcessId(hwnd, &pid) ||
        pid != GetCurrentProcessId())
        return false;
    char cls[32]{};
    return GetClassNameA(hwnd, cls, sizeof cls) > 0 && std::strcmp(cls, "ps3recomp_d3d12") == 0;
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

struct MouseCapture {
    HWND window = nullptr;
    POINT cursor{};
    RECT bounds{};
    bool borderless = false;
    bool clipped = false;
};

void release_cursor_clip(bool &cursor_clipped) {
    if (cursor_clipped) {
        ClipCursor(nullptr);
        cursor_clipped = false;
    }
}

void update_mouse(HWND hwnd, std::array<float, player::ACTION_COUNT> &v,
                  MouseCapture &capture,
                  float &mouse_virt_x, float &mouse_virt_y,
                  float &mouse_look_x, float &mouse_look_y,
                  int mouse_buttons, bool &out_in_client,
                  const VirtualMouse &vm, float dt_sec) {
    int raw_dx = 0, raw_dy = 0;
    float sens = (config.mouse_sensitivity > 0.01f ? config.mouse_sensitivity : 0.5f);

    RECT client{};
    POINT tl{0, 0}, br{0, 0};
    bool have_rect = hwnd && GetClientRect(hwnd, &client) &&
                     client.right > client.left && client.bottom > client.top;
    RECT screen_rect{};
    if (have_rect) {
        tl = {client.left, client.top};
        br = {client.right, client.bottom};
        have_rect = ClientToScreen(hwnd, &tl) && ClientToScreen(hwnd, &br);
        screen_rect = {tl.x, tl.y, br.x, br.y};
    }

    POINT cursor{};
    bool cursor_ok = GetCursorPos(&cursor) != FALSE;
    bool in_client = have_rect && cursor_ok && PtInRect(&screen_rect, cursor);
    out_in_client = in_client;

    if (vm.active) {
        release_cursor_clip(capture.clipped);
        raw_dx = vm.segs[vm.cur].dx;
        raw_dy = vm.segs[vm.cur].dy;
        capture.window = nullptr;
    } else {
        bool overlay_open = bakugan_savedata_overlay_active();
        bool alt_held = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        bool is_borderless = have_rect && ((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) == 0);

        if (!have_rect || !cursor_ok || overlay_open || alt_held) {
            release_cursor_clip(capture.clipped);
            capture.window = nullptr;
            player::step_mouse_virtual_stick(mouse_virt_x, mouse_virt_y, 0, 0, sens, dt_sec);
            player::step_mouse_virtual_stick(mouse_look_x, mouse_look_y, 0, 0, sens, dt_sec);
            return;
        }

        const bool changed = capture.window != hwnd || capture.borderless != is_borderless ||
                             !EqualRect(&capture.bounds, &screen_rect);
        if (changed) {
            release_cursor_clip(capture.clipped);
            capture.bounds = screen_rect;
            capture.borderless = is_borderless;
        }

        // Keep the cursor on the current monitor and discard movement from a window transition.
        if (is_borderless) {
            if (!capture.clipped && ClipCursor(&screen_rect))
                capture.clipped = true;
            in_client = true;
            out_in_client = true;
        } else {
            release_cursor_clip(capture.clipped);
            if (!in_client) {
                capture.window = nullptr;
                player::step_mouse_virtual_stick(mouse_virt_x, mouse_virt_y, 0, 0, sens, dt_sec);
                player::step_mouse_virtual_stick(mouse_look_x, mouse_look_y, 0, 0, sens, dt_sec);
                return;
            }
        }

        if (!changed) {
            raw_dx = cursor.x - capture.cursor.x;
            raw_dy = cursor.y - capture.cursor.y;
        }

        if (is_borderless) {
            POINT center{(screen_rect.left + screen_rect.right) / 2,
                         (screen_rect.top + screen_rect.bottom) / 2};
            if ((cursor.x != center.x || cursor.y != center.y) && SetCursorPos(center.x, center.y)) {
                capture.cursor = center;
            } else {
                capture.cursor = cursor;
            }
        } else {
            capture.cursor = cursor;
        }
        capture.window = hwnd;
    }

    bool held = config.mouse_look && (mouse_buttons & 2);
    if (held) {
        // Right click held: mouse look / camera aim (drives Left stick smoothly across 250 Hz ticks)
        player::step_mouse_virtual_stick(mouse_look_x, mouse_look_y, raw_dx, raw_dy, sens * 1.1f, dt_sec);
        v[player::L_LEFT] = std::max(v[player::L_LEFT], std::max(0.f, -mouse_look_x));
        v[player::L_RIGHT] = std::max(v[player::L_RIGHT], std::max(0.f, mouse_look_x));
        v[player::L_UP] = std::max(v[player::L_UP], std::max(0.f, -mouse_look_y));
        v[player::L_DOWN] = std::max(v[player::L_DOWN], std::max(0.f, mouse_look_y));
        player::step_mouse_virtual_stick(mouse_virt_x, mouse_virt_y, 0, 0, sens, dt_sec);
    } else {
        player::step_mouse_virtual_stick(mouse_virt_x, mouse_virt_y, raw_dx, raw_dy, sens, dt_sec);
        player::step_mouse_virtual_stick(mouse_look_x, mouse_look_y, 0, 0, sens, dt_sec);
    }
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
                                   float dt_sec, PortState &state) {
    std::array<float, player::ACTION_COUNT> gp_values{};
    std::array<float, 3> a{0.0f, 9.80665f, 0.0f}, g{};
    SDL_Gamepad *pad = device ? device->handle : nullptr;
    bool real = foreground && pad && config.motion != player::MotionMode::RightStick &&
                pads.motion(device, a, g);
    if (foreground && pad) {
        player::read_gamepad_actions(config, pad, gp_values);
    } else {
        state.pulse = {};
        state.sixaxis = {};
    }
    return to_host_report(player::make_gamepad_report(
        gp_values, config, real, a, g, dt_sec, now, state.sixaxis, state.pulse));
}
ps3_host_pad_report sample_keyboard(bool foreground, HWND hwnd, MouseCapture &capture,
                                    float &mouse_virt_x, float &mouse_virt_y,
                                    float &mouse_look_x, float &mouse_look_y,
                                    player::SixaxisState &kb_sixaxis,
                                    const VirtualMouse &vm, ULONGLONG now, float dt_sec,
                                    player::MotionPulse &pulse) {
    std::array<float, player::ACTION_COUNT> kb_values{};
    const bool overlay_open = bakugan_savedata_overlay_active();
    if (foreground && !overlay_open) {
        for (int i = 0; i < player::ACTION_COUNT; ++i)
            kb_values[i] = player::binding_strength(config.keyboard[i], nullptr, true, config.deadzone);
        int mouse_buttons = ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0) |
                            ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) ? 2 : 0) |
                            ((GetAsyncKeyState(VK_MBUTTON) & 0x8000) ? 4 : 0);
        if (vm.active) mouse_buttons = vm.segs[vm.cur].buttons;
        bool in_client = false;
        update_mouse(hwnd, kb_values, capture,
                    mouse_virt_x, mouse_virt_y, mouse_look_x, mouse_look_y,
                     mouse_buttons, in_client, vm, dt_sec);
        if (config.mouse_left_cross && (mouse_buttons & 1) && (vm.active || in_client))
            kb_values[player::CROSS] = 1.0f;
    } else {
        release_cursor_clip(capture.clipped);
        capture.window = nullptr;
        mouse_virt_x = 0.0f;
        mouse_virt_y = 0.0f;
        mouse_look_x = 0.0f;
        mouse_look_y = 0.0f;
        kb_sixaxis = {};
        pulse = {};
    }
    ps3_host_pad_report result = to_host_report(player::make_keyboard_mouse_report(
        kb_values, mouse_virt_x, mouse_virt_y, config, dt_sec, now, kb_sixaxis, pulse,
        foreground && !overlay_open));
    if (foreground && !overlay_open && vm.active)
        result.buttons |= (uint16_t)vm.segs[vm.cur].pad;
    return result;
}
unsigned __stdcall run(void *) {
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    player::MotionPulse kb_pulse;
    player::SixaxisState kb_sixaxis;
    MouseCapture capture;
    float mouse_virt_x = 0.0f;
    float mouse_virt_y = 0.0f;
    float mouse_look_x = 0.0f;
    float mouse_look_y = 0.0f;
    ULONGLONG refresh = 0;
    ULONGLONG script_poll = 0;
    auto prev_tick = std::chrono::steady_clock::now();
    VirtualMouse vm;
    std::string prior;
    struct AppliedRumble {
        SDL_JoystickID device_id = 0;
        uint16_t lo = 0;
        uint16_t hi = 0;
        ULONGLONG last_sent_ms = 0;
    };
    std::array<AppliedRumble, MAX_PORTS> applied_rumble{};
    while (WaitForSingleObject(stop_event, 4) == WAIT_TIMEOUT) {
        auto tick_now = std::chrono::steady_clock::now();
        float dt_sec = std::clamp(
            std::chrono::duration<float>(tick_now - prev_tick).count(), 0.001f, 0.05f);
        prev_tick = tick_now;
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
        const player::ButtonPromptStyle detected_style = !ordered.empty()
            ? player::prompt_style_from_gamepad_type(ordered[0]->type)
            : player::ButtonPromptStyle::PlayStation;
        publish_prompt_style(player::resolve_prompt_style(config.button_prompts, detected_style));
        HWND hwnd = nullptr;
        bool foreground = focused(hwnd);
        if (!allow_keyboard || !foreground) {
            release_cursor_clip(capture.clipped);
            capture.window = nullptr;
        }
        std::array<PortState, MAX_PORTS> next_ports{};
        int next_idx = 0;
        std::string state;
        // VirtualMouse scripts always drive port 0 alongside whatever controller is there.
        if (vm.active && allow_keyboard) {
            ps3_host_pad_report kb = sample_keyboard(foreground, hwnd, capture, mouse_virt_x, mouse_virt_y,
                mouse_look_x, mouse_look_y, kb_sixaxis, vm, now, dt_sec, kb_pulse);
            next_ports[0].connected = true;
            next_ports[0].report = kb;
            next_idx = 1;
            state = "P1: keyboard/mouse (script)";
        } else {
            for (size_t i = 0; i < ordered.size() && next_idx < MAX_PORTS; ++i) {
                if (ports[next_idx].is_gamepad && ports[next_idx].device_id == ordered[i]->id)
                    next_ports[next_idx] = ports[next_idx];
                next_ports[next_idx].connected = true;
                next_ports[next_idx].is_gamepad = true;
                next_ports[next_idx].device_id = ordered[i]->id;
                next_ports[next_idx].report = sample_gamepad(ordered[i], foreground, now, dt_sec, next_ports[next_idx]);
                if (!state.empty()) state += " | ";
                state += "P" + std::to_string(next_idx + 1) + ": " + ordered[i]->name +
                         " (gyro=" + std::to_string(ordered[i]->gyro ? 1 : 0) +
                         " accel=" + std::to_string(ordered[i]->accel ? 1 : 0) +
                         " sensors=" + std::to_string(ordered[i]->sensors_enabled ? 1 : 0) + ")";
                ++next_idx;
            }
            if (allow_keyboard && next_idx < MAX_PORTS) {
                next_ports[next_idx].connected = true;
                next_ports[next_idx].is_gamepad = false;
                next_ports[next_idx].report = sample_keyboard(foreground, hwnd, capture, mouse_virt_x, mouse_virt_y,
                    mouse_look_x, mouse_look_y, kb_sixaxis, vm, now, dt_sec, kb_pulse);
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

        std::array<RumbleTarget, MAX_PORTS> targets{};
        AcquireSRWLockExclusive(&rumble_lock);
        for (int i = 0; i < MAX_PORTS; ++i) {
            targets[i] = rumble_targets[i];
            rumble_targets[i].dirty = false;
        }
        ReleaseSRWLockExclusive(&rumble_lock);

        const bool overlay_open = bakugan_savedata_overlay_active();
        for (int p = 0; p < MAX_PORTS; ++p) {
            const SDL_JoystickID cur_id = next_ports[p].is_gamepad ? next_ports[p].device_id : 0;
            const SDL_JoystickID old_id = applied_rumble[p].device_id;
            if (old_id != cur_id) {
                if (old_id && (applied_rumble[p].lo || applied_rumble[p].hi)) {
                    bool still_assigned = false;
                    for (int q = 0; q < MAX_PORTS; ++q) {
                        if (next_ports[q].is_gamepad && next_ports[q].device_id == old_id) {
                            still_assigned = true;
                            break;
                        }
                    }
                    if (!still_assigned) {
                        if (SDL_Gamepad *old_pad = pads.handle_by_id(old_id))
                            SDL_RumbleGamepad(old_pad, 0, 0, 0);
                    }
                }
                applied_rumble[p] = {cur_id, 0, 0, 0};
            }
        }
        for (int p = 0; p < MAX_PORTS; ++p) {
            const SDL_JoystickID cur_id = next_ports[p].is_gamepad ? next_ports[p].device_id : 0;
            SDL_Gamepad *cur_pad = cur_id ? pads.handle_by_id(cur_id) : nullptr;
            uint16_t eff_lo = 0, eff_hi = 0;
            if (cur_pad && foreground && !overlay_open) {
                player::scale_rumble_motors(targets[p].lo, targets[p].hi,
                                            config.vibration_strength, eff_lo, eff_hi);
            }
            const bool active = (eff_lo != 0 || eff_hi != 0);
            const bool changed = (eff_lo != applied_rumble[p].lo ||
                                  eff_hi != applied_rumble[p].hi ||
                                  (targets[p].dirty && active));
            const bool renew = active && (applied_rumble[p].last_sent_ms == 0 ||
                                          now - applied_rumble[p].last_sent_ms >= RUMBLE_RENEW_MS);
            if (cur_pad && (changed || renew)) {
                if (active) {
                    SDL_RumbleGamepad(cur_pad, eff_lo, eff_hi, RUMBLE_HOLD_MS);
                    applied_rumble[p].last_sent_ms = now;
                } else {
                    SDL_RumbleGamepad(cur_pad, 0, 0, 0);
                    applied_rumble[p].last_sent_ms = 0;
                }
                applied_rumble[p].lo = eff_lo;
                applied_rumble[p].hi = eff_hi;
                applied_rumble[p].device_id = cur_id;
            } else if (!cur_pad) {
                applied_rumble[p] = {0, 0, 0, 0};
            }
        }

        if (state != prior) {
            fprintf(stderr, "[input] %s\n", state.c_str());
            prior = state;
        }
    }
    release_cursor_clip(capture.clipped);
    pads.stop_all_rumble();
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
    config.vibration_strength = player::resolve_vibration_strength(config);
    config.button_prompts = player::resolve_button_prompts(config);
    std::string sdl_error;
    if (!pads.init(sdl_error)) {
        fprintf(stderr, "[input] SDL3 init failed: %s\n", sdl_error.c_str());
        return false;
    }
    {
        player::Device *preferred = (config.input != player::InputMode::KeyboardMouse)
                                       ? pads.selected(config)
                                       : nullptr;
        const player::ButtonPromptStyle detected = preferred
            ? player::prompt_style_from_gamepad_type(preferred->type)
            : player::ButtonPromptStyle::PlayStation;
        publish_prompt_style(player::resolve_prompt_style(config.button_prompts, detected));
    }
    for (int i = 0; i < MAX_PORTS; ++i) {
        ports[i] = {};
        ports[i].report = neutral_report();
        rumble_targets[i] = {};
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
    ps3_pad_set_host_actuator(actuator_sink, nullptr);
    return true;
}
void bakugan_input_shutdown() {
    if (!installed)
        return;
    ps3_pad_set_host_actuator(nullptr, nullptr);
    ps3_pad_set_host_provider(nullptr, nullptr);
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
int bakugan_input_button_prompt_style() {
    return active_prompt_style.load(std::memory_order_relaxed);
}
