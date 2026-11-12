#include "libs/input/cellPad.h"
#include "include/ps3emu/host_input.h"
#include "include/ps3emu/endian.h"
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
int main() {
    for (auto key : {"PAD_RECORD", "PAD_REPLAY", "PAD_FILE", "PAD_AUTOPRESS", "PAD_SCRIPT", "PAD_STICK",
                     "PAD_SWEEP", "YDKJ_INJECT_PAD"})
        _putenv_s(key, "");
    ps3_pad_set_host_provider(provide, nullptr);
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
    cellPadEnd();
    std::printf("cellPad multi-port provider / GetInfo / GetData / Sixaxis: %s\n",
                ok ? "PASS" : "FAIL");
    return ok ? 0 : 3;
}
