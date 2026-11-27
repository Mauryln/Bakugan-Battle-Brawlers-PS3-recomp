#include "libs/input/cellPad.h"
#include "include/ps3emu/host_input.h"
#include "include/ps3emu/endian.h"
#include "../../bakugan-recomp/input_model.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
static std::array<unsigned char, 65536> guest{};
extern "C" {
uint8_t *vm_base = guest.data();
uint32_t g_ww_lo = 0xFFFFFFFFu, g_ww_hi = 0;
int g_resv_store_active = 0;
u32 cellGcm_flip_request_count(void) { return 0; }
void ppu_resv_break_store(uint64_t) {}
void ps3_ww_report_inline(uint32_t, uint64_t, int) {}
int spu_coh_is_reserved(uint32_t) { return 0; }
void spu_lockline_lock() {}
void spu_lockline_unlock() {}
void spu_coh_notify_write(uint32_t) {}
s32 cellPadGetInfo(CellPadInfo2* info);
}
static std::array<ps3_host_pad_report, 3> snapshots;
static std::array<bool, 3> snapshot_connected{true, false, false};
static int provide(u32 port, ps3_host_pad_report *out, void *) {
    if (port >= snapshot_connected.size() || !snapshot_connected[port])
        return 0;
    *out = snapshots[port];
    return 1;
}
struct ActuatorLog {
    u16 lo = 0;
    u16 hi = 0;
    int calls = 0;
};
static std::array<ActuatorLog, CELL_PAD_MAX_PORT_NUM> act_logs;
static int record_actuator(u32 port, u16 lo, u16 hi, void *) {
    if (port >= act_logs.size())
        return 0;
    act_logs[port].lo = lo;
    act_logs[port].hi = hi;
    act_logs[port].calls++;
    return 1;
}
int main() {
    for (auto key : {"PAD_RECORD", "PAD_REPLAY", "PAD_FILE", "PAD_AUTOPRESS", "PAD_SCRIPT", "PAD_STICK",
                     "PAD_SWEEP", "YDKJ_INJECT_PAD"})
        _putenv_s(key, "");
    _putenv_s("PAD_NO_KEYBOARD", "1");
    ps3_pad_set_host_provider(provide, nullptr);
    ps3_pad_set_host_actuator(record_actuator, nullptr);
    if (cellPadInit(7) != CELL_OK)
        return 1;
    cellPadSetPortSetting(0, CELL_PAD_SETTING_SENSOR_ON | CELL_PAD_SETTING_PRESS_ON);
    snapshots[0].buttons = CELL_PAD_CTRL_CROSS | CELL_PAD_CTRL_L2;
    snapshots[0].lx = 17;
    snapshots[0].ly = 128;
    snapshots[0].rx = 240;
    snapshots[0].ry = 99;
    snapshots[0].l2 = 77;
    snapshots[0].sensors[0] = 123;
    snapshots[0].sensors[1] = 399;
    snapshots[0].sensors[2] = 777;
    snapshots[0].sensors[3] = 635;
    snapshots[0].sensors_valid = 1;
    if (cellPadGetData(0, (CellPadData *)0x100) != CELL_OK)
        return 2;
    auto data = (CellPadData *)(guest.data() + 0x100);
    auto word = [&](int index) { return ps3_bswap16(data->button[index]); };
    bool ok = word(CELL_PAD_BTN_OFFSET_DIGITAL2) == 0x41 && word(CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X) == 17 &&
              word(CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X) == 240 && word(CELL_PAD_BTN_OFFSET_PRESS_L2) == 77 &&
              word(CELL_PAD_BTN_OFFSET_PRESS_CROSS) == 255 && word(CELL_PAD_BTN_OFFSET_SENSOR_X) == 123 &&
              word(CELL_PAD_BTN_OFFSET_SENSOR_Z) == 777 && word(CELL_PAD_BTN_OFFSET_SENSOR_G) == 635;

    // Multi-port test: ports 0, 1, 2 active; port 3 disconnected.
    snapshot_connected = {true, true, true};
    snapshots[1].buttons = CELL_PAD_CTRL_CIRCLE;
    snapshots[1].lx = 200;
    snapshots[1].ly = 128;
    snapshots[1].rx = 128;
    snapshots[1].ry = 128;
    snapshots[2].buttons = CELL_PAD_CTRL_START;
    snapshots[2].lx = 128;
    snapshots[2].ly = 128;
    snapshots[2].rx = 128;
    snapshots[2].ry = 128;
    std::this_thread::sleep_for(std::chrono::milliseconds(10)); // surpass PAD_POLL_INTERVAL_MS (4 ms) before next GetInfo/GetData

    if (cellPadGetInfo((CellPadInfo2 *)0x400) != CELL_OK)
        return 4;
    if (cellPadGetInfo2((CellPadInfo2 *)0x800) != CELL_OK)
        return 5;
    if (cellPadPeriphGetInfo((CellPadPeriphInfo *)0xC00) != CELL_OK)
        return 6;
    auto info1_now = ps3_bswap32(*(u32 *)(guest.data() + 0x404));
    auto info1_s1 = guest.data()[0x400 + 0x208 + 1];
    auto info1_s2 = guest.data()[0x400 + 0x208 + 2];
    auto info1_s3 = guest.data()[0x400 + 0x208 + 3];
    auto *info2 = (CellPadInfo2 *)(guest.data() + 0x800);
    auto *pinfo = (CellPadPeriphInfo *)(guest.data() + 0xC00);
    ok = ok && info1_now == 3 && info1_s1 == CELL_PAD_STATUS_CONNECTED &&
         info1_s2 == CELL_PAD_STATUS_CONNECTED && info1_s3 == CELL_PAD_STATUS_DISCONNECTED;
    ok = ok && ps3_bswap32(info2->now_connect) == 3 &&
         ps3_bswap32(info2->port_status[1]) == CELL_PAD_STATUS_CONNECTED &&
         ps3_bswap32(info2->port_status[2]) == CELL_PAD_STATUS_CONNECTED &&
         ps3_bswap32(info2->port_status[3]) == CELL_PAD_STATUS_DISCONNECTED;
    ok = ok && ps3_bswap32(pinfo->now_connect) == 3 &&
         ps3_bswap32(pinfo->port_status[2]) == CELL_PAD_STATUS_CONNECTED &&
         ps3_bswap32(pinfo->port_status[3]) == CELL_PAD_STATUS_DISCONNECTED;

    if (cellPadGetData(1, (CellPadData *)0x100) != CELL_OK)
        return 7;
    ok = ok && ps3_bswap32((u32)data->len) > 0 && word(CELL_PAD_BTN_OFFSET_DIGITAL2) == 0x20 &&
         word(CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X) == 200;
    if (cellPadGetData(2, (CellPadData *)0x100) != CELL_OK)
        return 8;
    ok = ok && ps3_bswap32((u32)data->len) > 0 && word(CELL_PAD_BTN_OFFSET_DIGITAL1) == 0x08;
    if (cellPadGetData(3, (CellPadData *)0x100) != CELL_OK)
        return 9;
    ok = ok && ps3_bswap32((u32)data->len) == 0;

    // Boundary check: 10-bit sensor extremes (0, 399, 512, 1023) and neutral fallback when sensors_valid=0
    snapshots[0].sensors[0] = 0;
    snapshots[0].sensors[1] = 1023;
    snapshots[0].sensors[2] = 512;
    snapshots[0].sensors[3] = 399;
    snapshots[0].sensors_valid = 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (cellPadGetData(0, (CellPadData *)0x100) != CELL_OK)
        return 10;
    ok = ok && word(CELL_PAD_BTN_OFFSET_SENSOR_X) == 0 && word(CELL_PAD_BTN_OFFSET_SENSOR_Y) == 1023 &&
         word(CELL_PAD_BTN_OFFSET_SENSOR_Z) == 512 && word(CELL_PAD_BTN_OFFSET_SENSOR_G) == 399;

    snapshots[0].sensors_valid = 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (cellPadGetData(0, (CellPadData *)0x100) != CELL_OK)
        return 11;
    ok = ok && word(CELL_PAD_BTN_OFFSET_SENSOR_X) == 512 && word(CELL_PAD_BTN_OFFSET_SENSOR_Y) == 399 &&
         word(CELL_PAD_BTN_OFFSET_SENSOR_Z) == 512 && word(CELL_PAD_BTN_OFFSET_SENSOR_G) == 512;

    // Calibrated Stick R full-deflection sensor words (right + up at 1.0x sensitivity)
    snapshots[0].sensors[0] = 625;
    snapshots[0].sensors[1] = 399;
    snapshots[0].sensors[2] = 399;
    snapshots[0].sensors[3] = 635;
    snapshots[0].sensors_valid = 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (cellPadGetData(0, (CellPadData *)0x100) != CELL_OK)
        return 12;
    ok = ok && word(CELL_PAD_BTN_OFFSET_SENSOR_X) == 625 && word(CELL_PAD_BTN_OFFSET_SENSOR_Y) == 399 &&
         word(CELL_PAD_BTN_OFFSET_SENSOR_Z) == 399 && word(CELL_PAD_BTN_OFFSET_SENSOR_G) == 635;

    auto *act = (CellPadActParam *)(guest.data() + 0xF00);
    auto set_act = [&](u32 port, u8 small_motor, u8 large_motor) {
        act->motor[CELL_PAD_ACTUATOR_PARAM_SMALL] = small_motor;
        act->motor[CELL_PAD_ACTUATOR_PARAM_LARGE] = large_motor;
        return cellPadSetActDirect(port, (CellPadActParam *)0xF00);
    };
    ok = ok && set_act(0, 1, 0) == CELL_OK &&
         act_logs[0].lo == 0 && act_logs[0].hi == 65535 && act_logs[0].calls == 1;
    ok = ok && set_act(0, 0, 128) == CELL_OK &&
         act_logs[0].lo == 128 * 257 && act_logs[0].hi == 0 && act_logs[0].calls == 2;
    {
        uint16_t scaled_lo = 0, scaled_hi = 0;
        player::scale_rumble_motors(act_logs[0].lo, act_logs[0].hi, 50, scaled_lo, scaled_hi);
        ok = ok && scaled_lo == 16448 && scaled_hi == 0;
    }
    ok = ok && set_act(0, 1, 255) == CELL_OK &&
         act_logs[0].lo == 65535 && act_logs[0].hi == 65535 && act_logs[0].calls == 3;
    {
        uint16_t lo0 = 0, hi0 = 0, lo50 = 0, hi50 = 0, lo100 = 0, hi100 = 0;
        player::scale_rumble_motors(act_logs[0].lo, act_logs[0].hi, 0, lo0, hi0);
        player::scale_rumble_motors(act_logs[0].lo, act_logs[0].hi, 50, lo50, hi50);
        player::scale_rumble_motors(act_logs[0].lo, act_logs[0].hi, 100, lo100, hi100);
        ok = ok && lo0 == 0 && hi0 == 0 &&
             lo50 == 32768 && hi50 == 22937 &&
             lo100 == 65535 && hi100 == 45875;
    }
    ok = ok && set_act(0, 255, 0) == CELL_OK &&
         act_logs[0].lo == 0 && act_logs[0].hi == 65535 && act_logs[0].calls == 4;
    ok = ok && set_act(0, 0, 0) == CELL_OK &&
         act_logs[0].lo == 0 && act_logs[0].hi == 0 && act_logs[0].calls == 5;

    // Multi-port isolation + invalid args
    ok = ok && set_act(1, 0, 64) == CELL_OK &&
         act_logs[1].lo == 64 * 257 && act_logs[1].hi == 0 && act_logs[1].calls == 1;
    ok = ok && set_act(2, 1, 200) == CELL_OK &&
         act_logs[2].lo == 200 * 257 && act_logs[2].hi == 65535 && act_logs[2].calls == 1;
    ok = ok && act_logs[0].calls == 5;
    ok = ok && set_act(CELL_PAD_MAX_PORT_NUM, 1, 1) == (s32)CELL_PAD_ERROR_INVALID_PARAMETER;
    ok = ok && cellPadSetActDirect(0, nullptr) == (s32)CELL_PAD_ERROR_INVALID_PARAMETER;
    ok = ok && act_logs[0].calls == 5;

    // Re-arm port 0 and confirm cellPadEnd stops all ports
    ok = ok && set_act(0, 1, 100) == CELL_OK && act_logs[0].calls == 6;
    std::array<int, CELL_PAD_MAX_PORT_NUM> pre_end_calls{};
    for (size_t i = 0; i < pre_end_calls.size(); ++i)
        pre_end_calls[i] = act_logs[i].calls;

    cellPadEnd();
    for (size_t i = 0; i < act_logs.size(); ++i) {
        ok = ok && act_logs[i].lo == 0 && act_logs[i].hi == 0 &&
             act_logs[i].calls == pre_end_calls[i] + 1;
    }
    ok = ok && set_act(0, 1, 1) == (s32)CELL_PAD_ERROR_NOT_OPENED;
    std::printf("cellPad multi-port provider / GetInfo / GetData / Sixaxis / Actuator: %s\n",
                ok ? "PASS" : "FAIL");
    return ok ? 0 : 3;
}
