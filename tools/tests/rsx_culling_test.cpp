#include <d3d12.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

typedef uint32_t u32;
static D3D12_RASTERIZER_DESC rasterizer(u32 cull)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    #include "culling.inc"
    return pd.RasterizerState;
}

static void require(bool condition, const char* message)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

int main(int argc, char** argv)
{
    bool flip = argc > 1 && !strcmp(argv[1], "flip");
    bool off = argc > 1 && !strcmp(argv[1], "off");
    // The VP viewport mapping reflects Y. A triangle's signed screen area
    // changes sign, so matching GL's CCW enum directly reverses kept faces.
    for (u32 ccw = 0; ccw < 2; ++ccw) {
        for (u32 front = 0; front < 2; ++front) {
            auto state = rasterizer(1u | (front << 1) | (ccw << 2));
            require(state.FrontCounterClockwise == ((ccw == 0) ^ flip),
                    "guest winding is reflected once for D3D viewport convention");
            require(state.CullMode == (off ? D3D12_CULL_MODE_NONE :
                        front ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_BACK),
                    "guest front/back culling selection is retained");
        }
    }
    require(rasterizer(0).CullMode == D3D12_CULL_MODE_NONE,
            "disabled guest culling remains double-sided");
    puts("PASS: viewport winding conversion preserves guest face selection");
}
