#include "libs/input/cellPad.h"
#include "include/ps3emu/host_input.h"
#include "include/ps3emu/endian.h"
#include <array>
#include <cstdio>
#include <cstdlib>
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
}
static ps3_host_pad_report snapshot;
static int provide(u32 port, ps3_host_pad_report *out, void *) {
    if (port)
        return 0;
    *out = snapshot;
    return 1;
}
int main() {
    for (auto key : {"PAD_RECORD", "PAD_REPLAY", "PAD_FILE", "PAD_AUTOPRESS", "PAD_SCRIPT", "PAD_STICK",
                     "PAD_SWEEP", "YDKJ_INJECT_PAD"})
        _putenv_s(key, "");
    ps3_pad_set_host_provider(provide, nullptr);
    if (cellPadInit(1) != CELL_OK)
        return 1;
    cellPadSetPortSetting(0, CELL_PAD_SETTING_SENSOR_ON | CELL_PAD_SETTING_PRESS_ON);
    snapshot.buttons = CELL_PAD_CTRL_CROSS | CELL_PAD_CTRL_L2;
    snapshot.lx = 17;
    snapshot.ly = 128;
    snapshot.rx = 240;
    snapshot.ry = 99;
    snapshot.l2 = 77;
    snapshot.sensors[0] = 123;
    snapshot.sensors[1] = 399;
    snapshot.sensors[2] = 777;
    snapshot.sensors[3] = 635;
    snapshot.sensors_valid = 1;
    if (cellPadGetData(0, (CellPadData *)0x100) != CELL_OK)
        return 2;
    auto data = (CellPadData *)(guest.data() + 0x100);
    auto word = [&](int index) { return ps3_bswap16(data->button[index]); };
    bool ok = word(CELL_PAD_BTN_OFFSET_DIGITAL2) == 0x41 && word(CELL_PAD_BTN_OFFSET_ANALOG_LEFT_X) == 17 &&
              word(CELL_PAD_BTN_OFFSET_ANALOG_RIGHT_X) == 240 && word(CELL_PAD_BTN_OFFSET_PRESS_L2) == 77 &&
              word(CELL_PAD_BTN_OFFSET_PRESS_CROSS) == 255 && word(CELL_PAD_BTN_OFFSET_SENSOR_X) == 123 &&
              word(CELL_PAD_BTN_OFFSET_SENSOR_Z) == 777 && word(CELL_PAD_BTN_OFFSET_SENSOR_G) == 635;
    cellPadEnd();
    std::printf("cellPad provider / big-endian report / analog pressure / Sixaxis: %s\n",
                ok ? "PASS" : "FAIL");
    return ok ? 0 : 3;
}
