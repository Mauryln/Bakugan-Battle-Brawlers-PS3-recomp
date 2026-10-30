#include <cstdint>
#include <cstdio>
#include <cstring>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;
#include "shader_cache_path.inc"

int main()
{
    // These can share the guest microcode while producing different host shaders.
    const char* shaders[] = {
        "Texture2D tex; float4 main():SV_Target{return tex.Load(int3(0,0,0));}",
        "TextureCube tex; float4 main():SV_Target{return float4(1,0,0,1);}",
        "Texture2D tex; float4 main():SV_Target{clip(-1);return 0;}",
        "Texture2D tex; float4 main():SV_Target{return float4(0,1,0,1);}",
        "Texture2D tex; float4 main():SV_Target{return float4(0,0,1,1);}",
    };
    char paths[5][520];
    for (unsigned i = 0; i < 5; ++i) {
        fp_shader_cache_path(paths[i], sizeof(paths[i]), shaders[i]);
        char repeated[520];
        fp_shader_cache_path(repeated, sizeof(repeated), shaders[i]);
        if (strcmp(paths[i], repeated)) {
            fprintf(stderr, "FAIL: identical final HLSL has unstable disk identity\n");
            return 1;
        }
        for (unsigned j = 0; j < i; ++j) {
            if (!strcmp(paths[i], paths[j])) {
                fprintf(stderr, "FAIL: final HLSL variants %u and %u reuse %s\n", j, i, paths[i]);
                return 1;
            }
        }
    }
    puts("PASS: disk cache distinguishes final HLSL and preserves identical source identity");
}
