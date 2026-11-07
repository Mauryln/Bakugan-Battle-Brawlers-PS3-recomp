static u32 upload_native_strip_vp_indexed(const rsx_state* state, u32 first, u32 count)
{
    extern uint8_t* vm_base;
    if (vp_canary_skip()) return 0;
    if (!state || !vm_base || !s_d3d.vp_vb_mapped ||
        !state->vertex_attribs[0].enabled) return 0;
    static struct {
        int valid;
        u32 epoch, next, length, previous[2];
    } strip;
    if (!strip.valid || strip.epoch != state->begin_epoch || strip.next != first) {
        strip.valid = 1;
        strip.epoch = state->begin_epoch;
        strip.length = 0;
    }
    strip.next = first + count;
    /* Seed references come from the previous packet and are fetched too;
     * leaving them out of the page range is how a cached run survives a
     * rewrite it depends on (the flicker). */
    if (strip.length >= 1) {
        for (int _s = 0; _s < 2; ++_s) {
            u32 r = strip.previous[_s];
            if (r < s_fetch_min_idx) s_fetch_min_idx = r;
            if (r > s_fetch_max_idx) s_fetch_max_idx = r;
        }
    }
    const int is_u16 = ((state->index_array_dma >> 4) & 0xF) == 1;
    const int restart = state->restart_index_enable &&
        (!is_u16 || state->restart_index <= 0xFFFFu);
    u32 maxv = (s_d3d.vp_vertex_capacity * VP_VERT_STRIDE - s_d3d.vp_vb_offset) / VP_VERT_STRIDE;
    VPSlot* out = (VPSlot*)((u8*)s_d3d.vp_vb_mapped
        + s_d3d.vp_vb_offset);
    VPSlot vertex[16], last[16], carry[16];
    VPSlot cache[16][16];
    u32 cache_tags[16], cache_valid = 0;
    u32 o = 0;
    int seeded = 0, cut = 0;
#define VP_STRIP_EMIT(v) do { memcpy(&out[o * 16], (v), s_vp_copy_bytes); ++o; } while (0)
#define VP_STRIP_FETCH(index, dst) do { \
    u32 ref = (index), slot = ref & 15u, bit = 1u << slot; \
    if (!(cache_valid & bit) || cache_tags[slot] != ref) { \
        read_vp_vertex(state, ref, cache[slot]); \
        cache_tags[slot] = ref; cache_valid |= bit; \
    } \
    memcpy((dst), cache[slot], s_vp_copy_bytes); \
    } while (0)
    if (vp_strip_direct()) {
        /* Candidate path: identical emission sequence, but each vertex is
         * fetched straight into its destination slot instead of being copied
         * through the 16-entry cache and a stack temporary. Vertex indices are
         * monotonic in a strip, so the cache only ever paid for the seed. */
        const int chk = vpchk_on();
        u32 checked = 0;
        for (u32 k = 0; k < count; ++k) {
            u32 index = read_guest_index(state, first + k);
            if (restart && index == state->restart_index) {
                strip.length = 0; cut = 1; continue;
            }
            u32 needed = 1;
            if (!seeded) needed += strip.length >= 2 ? 2 + (strip.length & 1u) : strip.length;
            if (cut && o) needed += 2 + (o & 1u);
            if (o + needed > maxv) break;
            if (!seeded) {
                if (strip.length >= 2) {
                    read_vp_vertex(state, strip.previous[0], &out[o * 16]); ++o;
                    if (strip.length & 1u) {
                        memcpy(&out[o * 16], &out[(o - 1) * 16], sizeof(VPSlot) * 16); ++o;
                    }
                    read_vp_vertex(state, strip.previous[1], &out[o * 16]); ++o;
                } else if (strip.length == 1) {
                    read_vp_vertex(state, strip.previous[1], &out[o * 16]); ++o;
                }
                seeded = 1;
            }
            if (cut && o) {
                u32 old_count = o;
                memcpy(&out[o * 16], last, sizeof(VPSlot) * 16); ++o;
                read_vp_vertex(state, index, &out[o * 16]); ++o;
                if (old_count & 1u) {
                    memcpy(&out[o * 16], &out[(o - 1) * 16], sizeof(VPSlot) * 16); ++o;
                }
                memcpy(last, &out[(o - 1) * 16], sizeof(VPSlot) * 16);
            } else {
                read_vp_vertex(state, index, &out[o * 16]); ++o;
                if (restart) memcpy(last, &out[(o - 1) * 16], sizeof(VPSlot) * 16);
                if (chk && checked < 200) {
                    vpchk_compare(state, index, &out[(o - 1) * 16]); ++checked;
                }
            }
            cut = 0;
            strip.previous[0] = strip.previous[1];
            strip.previous[1] = index;
            strip.length++;
        }
    } else
    for (u32 k = 0; k < count; ++k) {
        u32 index = read_guest_index(state, first + k);
        if (index < s_fetch_min_idx) s_fetch_min_idx = index;
        if (index > s_fetch_max_idx) s_fetch_max_idx = index;
        if (restart && index == state->restart_index) {
            strip.length = 0;
            cut = 1;
            continue;
        }
        u32 needed = 1;
        if (!seeded) needed += strip.length >= 2 ? 2 + (strip.length & 1u) : strip.length;
        if (cut && o) needed += 2 + (o & 1u);
        if (o + needed > maxv) break;
        if (!seeded) {
            if (strip.length >= 2) {
                VP_STRIP_FETCH(strip.previous[0], carry);
                VP_STRIP_EMIT(carry);
                if (strip.length & 1u) VP_STRIP_EMIT(carry);
                VP_STRIP_FETCH(strip.previous[1], carry);
                VP_STRIP_EMIT(carry);
            } else if (strip.length == 1) {
                VP_STRIP_FETCH(strip.previous[1], carry);
                VP_STRIP_EMIT(carry);
            }
            seeded = 1;
        }
        double _vt0 = (perf_on() && ((k & 63u) == 0)) ? perf_now() : 0.0;
        /* Batched decode: four consecutive strip indices through one attribute
         * descriptor pass. Only when nothing complicates the emit (no restart
         * cut pending, stream already seeded, room for four outputs). The
         * per-vertex path below stays the fallback, and VPCHK checks every
         * batched vertex against the slow reference. */
        if (!cut && seeded && !restart && (k + 3u) < count && (o + 4u) <= maxv) {
            u32 vi4[4];
            vi4[0] = index;
            vi4[1] = read_guest_index(state, first + k + 1u);
            vi4[2] = read_guest_index(state, first + k + 2u);
            vi4[3] = read_guest_index(state, first + k + 3u);
            for (u32 q = 0; q < 4; ++q) {
                if (vi4[q] < s_fetch_min_idx) s_fetch_min_idx = vi4[q];
                if (vi4[q] > s_fetch_max_idx) s_fetch_max_idx = vi4[q];
            }
            vp_fetch_vertex4(state, vi4, &out[o * 16]);
            if (vpchk_on() && ((k & 31u) == 0))
                for (u32 q = 0; q < 4; ++q)
                    vpchk_compare(state, vi4[q], &out[(o + q) * 16]);
            o += 4;
            k += 3;
            memcpy(last, &out[(o - 1) * 16], sizeof(VPSlot) * 16);
            strip.previous[0] = vi4[2];
            strip.previous[1] = vi4[3];
            strip.length += 4;
            continue;
        }
        VP_STRIP_FETCH(index, vertex);
        if (_vt0 > 0.0) s_perf_vfetch += (perf_now() - _vt0) * 64.0;
        if (cut && o) {
            u32 old_count = o;
            VP_STRIP_EMIT(last);
            VP_STRIP_EMIT(vertex);
            if (old_count & 1u) VP_STRIP_EMIT(vertex);
        }
        cut = 0;
        VP_STRIP_EMIT(vertex);
        memcpy(last, vertex, sizeof(last));
        strip.previous[0] = strip.previous[1];
        strip.previous[1] = index;
        strip.length++;
    }
#undef VP_STRIP_EMIT
#undef VP_STRIP_FETCH
    s_d3d.vp_vb_offset += o * VP_VERT_STRIDE;
    return o;
}
