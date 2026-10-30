#include <cmath>
#include <climits>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "polygon_types.inc"
#include "polygon_offset.inc"

static int method(rsx_state* state, u32 method, u32 data)
{
    #include "polygon_commands.inc"
    return 0;
}

static u32 bits(float value) { u32 result; memcpy(&result, &value, 4); return result; }
static void require(bool condition, const char* message)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

int main()
{
    rsx_state state = {};
    method(&state, 0x0A68, 1);
    method(&state, 0x0A78, bits(4.0f));
    method(&state, 0x0A7C, bits(6000.0f));
    require(state.polygon_offset_fill_enable == 1 && state.polygon_offset_scale == 4.0f &&
            state.polygon_offset_bias == 6000.0f, "guest commands retain polygon offset float bit patterns");
    int bias = 99; float slope = 99;
    rsx_polygon_offset(&state, &bias, &slope);
    require(bias == 6000 && slope == 4.0f, "enabled polygon fill offset reaches rasterizer units and slope");
    method(&state, 0x0A68, 0);
    rsx_polygon_offset(&state, &bias, &slope);
    require(bias == 0 && slope == 0.0f, "disabled offset clears rasterizer bias despite retained guest values");
    method(&state, 0x0A68, 1);
    method(&state, 0x0A78, bits(-4.0f));
    method(&state, 0x0A7C, bits(-6000.0f));
    rsx_polygon_offset(&state, &bias, &slope);
    require(bias == -6000 && slope == -4.0f, "negative guest offset preserves its sign");
    method(&state, 0x0A7C, bits(-4.6f));
    rsx_polygon_offset(&state, &bias, &slope);
    require(bias == -5, "fractional guest depth units round to nearest rasterizer integer");
    method(&state, 0x0A7C, bits(FLT_MAX));
    rsx_polygon_offset(&state, &bias, &slope);
    require(bias == INT_MAX, "finite positive bias clamps before integer conversion");
    method(&state, 0x0A7C, bits(-FLT_MAX));
    rsx_polygon_offset(&state, &bias, &slope);
    require(bias == INT_MIN, "finite negative bias clamps before integer conversion");
    method(&state, 0x0A78, bits(std::numeric_limits<float>::quiet_NaN()));
    method(&state, 0x0A7C, bits(std::numeric_limits<float>::infinity()));
    rsx_polygon_offset(&state, &bias, &slope);
    require(bias == 0 && slope == 0.0f, "nonfinite guest offset is sanitized before PSO creation");
    rsx_polygon_offset(nullptr, &bias, &slope);
    require(bias == 0 && slope == 0.0f, "absent guest state uses neutral offset");
    puts("PASS: guest polygon-offset commands decode and preserve enabled depth bias safely");
}
