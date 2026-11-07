#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

static int s_pick_cache_enabled;
static const char* option_value;
static unsigned lookups, mode_lookups;
static const char* cache_option;
static const char* mock_getenv(const char* name)
{
    if (!std::strcmp(name, "RSX_PICK_CACHE")) { ++mode_lookups; return cache_option; }
    if (std::strcmp(name, "RSX_DBG_PICK")) std::exit(2);
    ++lookups;
    return option_value;
}
#define getenv mock_getenv
#include "pick_cache.inc"
#undef getenv

static void require(bool ok, const char* why)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    option_value = !std::strcmp(argv[1], "absent") ? nullptr :
                   !std::strcmp(argv[1], "empty") ? "" : argv[1];
    bool from_env = !std::strcmp(argv[2], "default") || !std::strcmp(argv[2], "offenv");
    cache_option = !std::strcmp(argv[2], "offenv") ? "0" : nullptr;
    s_pick_cache_enabled = from_env ? -1 : std::atoi(argv[2]);
    const int expected = option_value ? std::atoi(option_value) : -2;
    for (unsigned draw = 0; draw < 10000; ++draw) {
        int value = vp_pick_filter();
        require(value == expected, "filter value preserved");
        for (int first : {0, 17, 255})
            require((value == -2 || first == value) == (expected == -2 || first == expected),
                    "draw selection preserved");
    }
    unsigned expected_lookups = s_pick_cache_enabled || expected >= 0 ? 1 : 10000;
    require(lookups == expected_lookups, "one lookup cached; original negative path repeats");
    require(mode_lookups == (from_env ? 1u : 0u), "mode resolved once; default enabled and explicit zero honored");
    s_pick_cache_enabled = 1;
    for (unsigned draw = 0; draw < 10000; ++draw) require(vp_pick_filter() == expected, "toggle on value");
    require(lookups == expected_lookups, "toggle on stops repeated lookups");
    s_pick_cache_enabled = 0;
    require(vp_pick_filter() == expected, "toggle off value");
    require(lookups == expected_lookups + (expected < 0), "toggle off restores original lookup behavior");
    std::puts("PASS: values, draw filter, one-time lookup and A/B transitions");
}
