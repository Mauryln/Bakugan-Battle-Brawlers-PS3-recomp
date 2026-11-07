#pragma once
#include "ps3emu/host_input.h"
// Install on the runner's main thread, before guest execution. No SDL video.
bool bakugan_input_install();
void bakugan_input_shutdown();
// A configured keyboard-only mode owns the picker too, with a neutral pad.
bool bakugan_input_gamepad_snapshot(ps3_host_pad_report *out);
