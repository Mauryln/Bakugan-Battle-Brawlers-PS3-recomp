#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cstdio>
#include <cstdlib>
using u32 = unsigned;
using u64 = uint64_t;
#include "vertex_capacity.inc"
static void check(bool condition)
{
    if (!condition) { puts("FAIL: VP CPU growth corrupts recorded vertices"); exit(1); }
}
int main()
{
    check(vp_capacity_for(0, 1) == 1);
    check(vp_capacity_for(393216, 4194305) == 0);
    check(vp_capacity_for(393216, 393216) == 393216);
    check(vp_capacity_for(393216, 393217) >= 393217);
    check(vp_capacity_for(393216, 1000000) >= 1000000);
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 8192,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    check(memory != nullptr);
    DWORD old_protection;
    check(VirtualProtect(memory + 4096, 4096, PAGE_NOACCESS, &old_protection) != 0);
    constexpr u32 used = 3 * 256;
    unsigned char* source = memory + 4096 - used;
    memset(source, 0x37, used);
    std::vector<unsigned char> destination(8 * 256, 0xA5);
    vp_copy_vertex_prefix(destination.data(), source, used);
    for (u32 i = 0; i < used; ++i) check(destination[i] == 0x37);
    for (u32 i = used; i < destination.size(); ++i) check(destination[i] == 0xA5);
    vp_copy_vertex_prefix(nullptr, nullptr, 0);
    VirtualFree(memory, 0, MEM_RELEASE);
    puts("PASS: CPU growth preserves the live prefix without reading unused capacity or another frame");
}
