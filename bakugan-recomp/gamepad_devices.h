#pragma once
#include <SDL3/SDL.h>
#include "player_config.h"
#include <vector>
#include <array>
#include <string>
namespace player {
struct Device {
    SDL_JoystickID id = 0;
    SDL_Gamepad *handle = nullptr;
    std::string name, guid, serial;
    bool gyro = false, accel = false, sensors_enabled = false;
};
class Gamepads {
    bool initialized = false;
    std::vector<Device> items;

  public:
    bool init(std::string &error);
    void refresh();
    void close(bool quit_subsystem);
    const std::vector<Device> &devices() const { return items; }
    Device *selected(const Config &config);
    bool motion(Device *d, std::array<float, 3> &accel, std::array<float, 3> &gyro);
};
float binding_strength(Binding b, SDL_Gamepad *gamepad, bool keyboard, float deadzone);
std::wstring utf8_wide(const std::string &value);
} // namespace player
