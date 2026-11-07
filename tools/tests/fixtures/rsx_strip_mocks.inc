static VPSlot buffer[1024 * 16];
static struct { void* vp_vb_mapped = buffer; u32 vp_vb_offset = 0;
    u32 vp_vertex_capacity = 1024; } s_d3d;
uint8_t* vm_base = reinterpret_cast<uint8_t*>(buffer);
static u32 s_fetch_min_idx = ~0u, s_fetch_max_idx;
static u32 s_vp_copy_bytes = 256, revision, fetched;
static double s_perf_vfetch;
static bool vp_canary_skip() { return false; }
static bool vp_strip_direct() { return false; }
static bool perf_on() { return false; }
static double perf_now() { return 0; }
static bool vpchk_on() { return false; }
static void vpchk_compare(const rsx_state*, u32, VPSlot*) {}
static u32 read_guest_index(const rsx_state*, u32 i) { return indices.at(i); }
static void read_vp_vertex(const rsx_state*, u32 i, VPSlot* dst) {
    ++fetched;
    for (u32 a = 0; a < 64; ++a) {
        u32 word = i * 131u + a + revision * 7919u;
        memcpy(reinterpret_cast<u8*>(dst) + a * 4, &word, sizeof(word));
    }
}
static void vp_fetch_vertex4(const rsx_state* state, const u32 vi[4], VPSlot* dst) {
    for (u32 q = 0; q < 4; ++q) read_vp_vertex(state, vi[q], dst + q * 16);
}
