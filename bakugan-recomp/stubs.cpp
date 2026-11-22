/*
 * ps3recomp game project -- game-specific hooks and overrides.
 *
 * main.cpp is the runner and is game-agnostic. This file is where a port puts
 * everything that is not: the PRX loading hook it must answer, overrides for
 * firmware functions the HLE library gets wrong for this title, and patches
 * over recompiled code.
 *
 * It starts out as the empty version of all of that, which is what a game with
 * no lifted PRX and no overrides needs.
 */

#include <cstdio>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include "ppu_recomp.h"
#include "cellPad.h"

extern "C" void ppu_register_function(uint64_t, void (*)(ppu_context*));
extern "C" void ps3_hle_register_ctx(uint32_t, const char*, void (*)(ppu_context*));
extern "C" unsigned cellGcm_flip_request_count();
extern "C" uint64_t ppu_guest_call(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                  uint64_t, uint64_t, uint64_t, uint64_t);

static void trace_pad_report(ppu_context* ctx)
{
    const auto port = static_cast<u32>(ctx->gpr[3]);
    const auto data = static_cast<u32>(ctx->gpr[4]);
    const auto result = cellPadGetData(port, reinterpret_cast<CellPadData*>(static_cast<uintptr_t>(data)));
    ctx->gpr[3] = static_cast<int64_t>(result);
    if (result != CELL_OK || !data || port >= CELL_PAD_MAX_PORT_NUM)
        return;

    const u32 len = vm_read32(data + offsetof(CellPadData, len));
    const u16 buttons = vm_read16(data + offsetof(CellPadData, button) + CELL_PAD_BTN_OFFSET_DIGITAL1 * sizeof(u16)) &
                        (CELL_PAD_CTRL_START | CELL_PAD_CTRL_SELECT);
    static thread_local u16 previous[CELL_PAD_MAX_PORT_NUM][2]{};
    auto& last = previous[port][len != 0];
    if (last != buttons) {
        last = buttons;
        fprintf(stderr, "[PAUSE PAD] flip=%u port=%u len=%u buttons=%#x\n",
                cellGcm_flip_request_count(), port, len, static_cast<unsigned>(buttons));
    }
}

static int pause_trace_on()
{
    static int on = -1;
    if (on < 0) on = getenv("BAKUGAN_PAUSE_TRACE") ? 1 : 0;
    return on;
}

/* Last state of App::Game::CPauseTask: 0 is the one-shot init, 1 the closed
 * state that polls Start, 2+ the opening/open menu. The Start latch below only
 * runs while the menu is closed, so the extra pressed update cannot toggle an
 * already-open menu. */
static volatile u32 pause_task_state = ~0u;

/* CPauseTask (func_00114CB4, state 0) opens only when both pad queries return
 * 0x800: held (func_002A65C8 -> pad+0x10) and pressed (func_002A78B8 ->
 * pad+0x14). The pad update writes pressed for a single update, so the pause
 * task can read the pad object after that update and miss it. Keep Start's
 * pressed bit for one extra update; no other button or field is touched. */
static void pad_update_start_pressed(ppu_context* ctx)
{
    const auto pad = static_cast<u32>(ctx->gpr[3]);
    static unsigned latch[CELL_PAD_MAX_PORT_NUM]{};
    const unsigned slot = static_cast<unsigned>(vm_read32(pad + 4)) % CELL_PAD_MAX_PORT_NUM;

    const u32 previous = vm_read32(pad + 0x0c);
    func_002E3930(ctx);
    const u32 held = vm_read32(pad + 0x0c);
    const u32 pressed = vm_read32(pad + 0x14);

    if ((pressed & 0x800) && pause_task_state <= 1)
        latch[slot] = 2;
    if (latch[slot]) {
        vm_write32(pad + 0x14, pressed | 0x800);
        latch[slot]--;
    }

    if (!pause_trace_on())
        return;

    /* Invocation proof: the xref-based hook only fires if the guest dispatches
     * this method through the function table. */
    static unsigned calls;
    if (calls < 3 || (calls % 600) == 0)
        fprintf(stderr, "[PAUSE GUEST] flip=%u call=%u slot=%u len=%u d1=%#x\n",
                cellGcm_flip_request_count(), calls, vm_read32(pad + 0x04),
                vm_read32(pad + 0x1ac), vm_read16(pad + 0x1b4));
    calls++;

    constexpr u32 pause_buttons = 0xc00;
    if ((previous ^ held) & pause_buttons)
        fprintf(stderr, "[PAUSE GUEST] flip=%u slot=%u held=%#x pressed=%#x released=%#x repeat=%#x\n",
                cellGcm_flip_request_count(), vm_read32(pad + 0x04), held & pause_buttons,
                pressed & pause_buttons, vm_read32(pad + 0x18) & pause_buttons,
                vm_read32(pad + 0x1c) & pause_buttons);

    if (pressed & 0x800) {
        const u32 toc = static_cast<u32>(ctx->gpr[2]);
        const u32 obj = vm_read32(vm_read32(toc - 0x3810));
        if (obj) {
            const u32 vt = vm_read32(obj);
            const u32 opd1 = vm_read32(vt + 0x20);
            const u32 opd2 = vm_read32(vt + 0x24);
            const u32 app = vm_read32(vm_read32(toc - 0x3814));
            const u8 count = vm_read8(app + 0x22dd);
            const u32 entry0 = app + 0x1b90;
            const u8 conn0 = vm_read8(entry0 + 0x9c);
            const u8 port0 = vm_read8(entry0 + 0x97);
            const auto a = ppu_guest_call(opd1, obj, 0, 0, 0, 0, 0, 0, 0);
            const auto b = ppu_guest_call(opd2, obj, 0, 0, 0, 0, 0, 0, 0);
            fprintf(stderr, "[PAUSE INPUT] flip=%u obj=%08X vt=%08X opd=%08X/%08X players=%u p0conn=%u p0port=%u a=%#llx b=%#llx\n",
                    cellGcm_flip_request_count(), obj, vt, opd1, opd2, count, conn0, port0,
                    static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));
        }
    }
}

/* func_00114CB4 is the first virtual method of App::Game::CPauseTask (vtable
 * 0x686078 -> OPD 0x69C150 -> 0x00114CB4). It runs only while the pause menu
 * task is alive, so an update after a Start edge proves the menu was really
 * created -- independently of whether its frames become visible. */
static void trace_pause_task(ppu_context* ctx)
{
    static u32 last_flip = ~0u;
    static u32 last_state = ~0u;
    const u32 flip = cellGcm_flip_request_count();
    const auto self = static_cast<u32>(ctx->gpr[3]);
    const u32 state = vm_read32(self + 0x14);
    pause_task_state = state;
    if (pause_trace_on() &&
        (last_flip == ~0u || flip - last_flip > 20 || state != last_state)) {
        fprintf(stderr, "[PAUSE TASK] flip=%u state=%u object=%08X\n",
                flip, state, self);
    }
    last_flip = flip;
    last_state = state;
    func_00114CB4(ctx);
}

void bakugan_install_pause_trace()
{
    ppu_register_function(0x002E3930, pad_update_start_pressed);
    ppu_register_function(0x00114CB4, trace_pause_task);
    if (!pause_trace_on())
        return;
    ps3_hle_register_ctx(0x8B72CDA1, "cellPadGetData", trace_pad_report);
}

static void trace_boot_movies(ppu_context* ctx)
{
    const uint32_t self = (uint32_t)ctx->gpr[3];
    static uint32_t previous = ~0u;
    const uint32_t state = vm_read32(self + 0x14);
    if (state != previous) {
        fprintf(stderr, "[movie] boot sequence self=0x%08X state=%u\n", self, state);
        previous = state;
    }
    func_0019B8C4(ctx);
}

static void trace_movie_enter(ppu_context* ctx)
{
    fprintf(stderr, "[movie] enter self=0x%08X asset=0x%08X player=0x%08X\n",
            (uint32_t)ctx->gpr[3], vm_read32(ctx->gpr[3] + 0x2c),
            vm_read32(ctx->gpr[3] + 0x34));
    func_002AA48C(ctx);
    fprintf(stderr, "[movie] enter returned r3=0x%08X\n", (uint32_t)ctx->gpr[3]);
}

#include "ps3emu/ps3types.h"
#include "ps3emu/error_codes.h"

/* ---------------------------------------------------------------------------
 * PRX loading
 *
 * main.cpp calls this once, after the lifted function table is registered and
 * vm_base is live, before the game runs. A title that lifts a real system PRX
 * -- libsre, which is cellSpurs and cellSync -- loads its image into guest RAM
 * here and registers its exports. Most ports never need to: the HLE library
 * answers those modules instead.
 * -----------------------------------------------------------------------*/

extern "C" void ps3_load_prx_modules(void)
{
    if (getenv("BAKUGAN_VIDEO_TRACE"))
    {
        ppu_register_function(0x002AA48C, trace_movie_enter);
        ppu_register_function(0x0019B8C4, trace_boot_movies);
    }
}

/* ---------------------------------------------------------------------------
 * Example: Override a specific NID
 *
 * The module system resolves function imports by NID.  To replace a default
 * HLE stub with custom logic, register your function at init time.
 * -----------------------------------------------------------------------*/

/*
 * Example: override cellGcmSetFlipMode (NID 0xA53D12AE) to force vsync.
 *
 *   static void my_cellGcmSetFlipMode(u32 mode)
 *   {
 *       printf("[hook] cellGcmSetFlipMode: forcing VSYNC (was %u)\n", mode);
 *       // Force vsync regardless of what the game requested
 *       cellGcmSetFlipMode(CELL_GCM_DISPLAY_VSYNC);
 *   }
 *
 * Then in your init code:
 *
 *   ps3::modules::override_nid(0xA53D12AE,
 *       reinterpret_cast<void*>(my_cellGcmSetFlipMode));
 */

/* ---------------------------------------------------------------------------
 * Example: Hook into module loading
 *
 * You can intercept cellSysmoduleLoadModule to perform custom actions
 * when a particular module is loaded.
 * -----------------------------------------------------------------------*/

/*
 *   static s32 my_loadmodule_hook(u16 id)
 *   {
 *       printf("[hook] Module 0x%04X loaded\n", id);
 *
 *       if (id == CELL_SYSMODULE_AUDIO) {
 *           // Perform custom audio backend initialization
 *           init_my_audio_backend();
 *       }
 *
 *       // Call the original implementation
 *       return cellSysmoduleLoadModule(id);
 *   }
 */

/* ---------------------------------------------------------------------------
 * Game-specific patches
 *
 * Sometimes you need to patch recompiled code at specific addresses.
 * Use the patch system to modify behavior without editing generated code.
 * -----------------------------------------------------------------------*/

/*
 *   // Skip an integrity check at guest address 0x00812FA0
 *   ps3::patches::nop_range(0x00812FA0, 0x00812FB0);
 *
 *   // Replace a function call with a direct return
 *   ps3::patches::force_return(0x009A1000, 0);  // return 0
 */
