#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "spu_context.h"
#include "ppu_context.h"
#include "ps3emu/error_codes.h"
#include "ps3emu/log_first.h"
typedef struct { int unused; } spu_thread_t;
static SRWLOCK s_spu_port_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE s_spu_port_bound = CONDITION_VARIABLE_INIT;
#define SPU_PORT_BIND_WAIT_MS 0
static spu_thread_t* spu_find_thread(uint32_t id) { (void)id; return NULL; }
static uint32_t spu_port_queue_locked(const spu_thread_t* t, uint32_t g, unsigned p)
{ (void)t; (void)g; (void)p; return 0; }
static uint32_t queue, flag_bits;
uint32_t spurs_port_queue(uint32_t p) { (void)p; return queue; }
int64_t sys_event_flag_set(ppu_context* c) { flag_bits = (uint32_t)c->gpr[4]; return CELL_OK; }
static int pushed;
int sys_event_queue_push_by_id(uint32_t q, uint64_t source, uint64_t d1, uint64_t d2, uint64_t d3)
{ (void)source; (void)d1; (void)d2; (void)d3; pushed = q != 0; return 0; }
#include "spu_deliver_user_event.inc"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL: %s\n",#c); return 1; } } while (0)
int main(void)
{
    spu_context* ctx = calloc(1, sizeof(*ctx));
    CHECK(ctx);
    spu_channel_write(&ctx->ch_out_mbox, 0x1234);
    CHECK(spu_deliver_user_event(ctx, 0x01000000) == 1);
    CHECK(ctx->ch_out_mbox.count == 0 && ctx->ch_in_mbox.count == 1);
    CHECK(spu_channel_read(&ctx->ch_in_mbox) == CELL_ENOTCONN);
    spu_channel_write(&ctx->ch_out_mbox, 0x1234);
    CHECK(spu_deliver_user_event(ctx, 0x41000000) == 1);
    CHECK(ctx->ch_out_mbox.count == 0 && ctx->ch_in_mbox.count == 0);
    queue = 77;
    spu_channel_write(&ctx->ch_out_mbox, 0x1234);
    CHECK(spu_deliver_user_event(ctx, 0x01000000) == 1 && pushed);
    CHECK(spu_channel_read(&ctx->ch_in_mbox) == CELL_OK);
    queue = 0;
    spu_channel_write(&ctx->ch_out_mbox, 9);
    CHECK(spu_deliver_user_event(ctx, 0x80000003) == 1 && flag_bits == 8);
    CHECK(spu_channel_read(&ctx->ch_in_mbox) == CELL_OK);
    free(ctx);
    puts("PASS: SPURS send/throw and flag acknowledgements, including unbound ports");
    return 0;
}
