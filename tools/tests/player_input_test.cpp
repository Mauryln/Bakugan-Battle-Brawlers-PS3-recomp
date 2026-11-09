#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
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
    c.cap = L"60";
    c.deadzone = 0.22f;
    c.keyboard[CROSS] = {BindingKind::Key, 'B'};
    c.gamepad[CROSS] = {BindingKind::Button, SDL_GAMEPAD_BUTTON_WEST};
    c.device_guid = "test-guid";
    c.device_serial = "serial";
    c.motion = MotionMode::RightStick;
    std::wstring error;
    CHECK(save(c, path.wstring(), &error));
    Config loaded;
    CHECK(load(loaded, path.wstring(), &error));
    CHECK(loaded.language == c.language && loaded.output == c.output && loaded.cap == c.cap);
    CHECK(std::abs(loaded.deadzone - c.deadzone) < 0.001f && loaded.motion == c.motion);
    CHECK(loaded.keyboard[CROSS].code == 'B' && loaded.gamepad[CROSS].code == SDL_GAMEPAD_BUTTON_WEST);
    CHECK(loaded.device_guid == c.device_guid && loaded.device_serial == c.device_serial);
    CHECK(WritePrivateProfileStringW(L"Graphics", L"Sharpness", L"nan", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Graphics", L"Output", L"nonsense", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Keyboard", L"14", L"key:9999", path.c_str()));
    CHECK(load(loaded, path.wstring(), &error));
    CHECK(loaded.sharpness == 0.6f && loaded.output == L"720p");
    CHECK(loaded.keyboard[CROSS].code == 'X');
    CHECK(WritePrivateProfileStringW(L"Player", L"Version", L"999", path.c_str()));
    CHECK(!load(loaded, path.wstring(), &error));
    c.language = L"auto";
    SetEnvironmentVariableW(L"PAD_AUTOPRESS", L"1");
    SetEnvironmentVariableW(L"PS3_LANG", L"de");
    apply_environment(c, path.wstring());
    wchar_t env[128];
    CHECK(!GetEnvironmentVariableW(L"PAD_AUTOPRESS", env, 128));
    CHECK(!GetEnvironmentVariableW(L"PS3_LANG", env, 128));
    CHECK(GetEnvironmentVariableW(L"PS3_OUTPUT", env, 128) && std::wstring(env) == L"1440p");
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
    CHECK(r.sensors[0] == 0 && r.sensors[1] == 1023);
    v[MOTION_THROW] = 1;
    CHECK(make_report(v, MotionMode::RightStick, false, a, g, 1000, pulse).sensors[2] == 1023);
    CHECK(make_report(v, MotionMode::RightStick, false, a, g, 1100, pulse).sensors[2] == 0);
    CHECK(make_report(v, MotionMode::RightStick, false, a, g, 1200, pulse).sensors[2] == 512);
    CHECK(make_report(v, MotionMode::RightStick, false, a, g, 1400, pulse).sensors[2] == 512);
    v[MOTION_THROW] = 0;
    make_report(v, MotionMode::RightStick, false, a, g, 1500, pulse);
    v[MOTION_THROW] = 1;
    CHECK(make_report(v, MotionMode::RightStick, false, a, g, 1600, pulse).sensors[2] == 1023);
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
    std::printf("Player config / input model: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
