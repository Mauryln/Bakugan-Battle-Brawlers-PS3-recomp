#include <d3d12.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "rsx_texture_layout.h"
#include "texture_mips.inc"

static void require(bool condition, const char* message)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static void verify(ID3D12Device* device, bool cube)
{
    const u32 format = cube ? 0x88 : 0xA5; // BC3 cube / linear ARGB8 2D
    const u32 faces = cube ? 6 : 1;
    const u32 width = cube ? 8 : 4;
    const u32 mip_count = cube ? 4 : 3;
    rsx_tex_level levels[RSX_MAX_TEXTURE_LEVELS];
    u32 count = rsx_texture_mip_chain(format, width, width, mip_count, 0, levels);
    require(count == mip_count, "requested mip chain is retained");
    u32 face_stride = cube ? rsx_texture_cube_face_stride(format, width, width, count, 0)
                          : levels[count - 1].offset + levels[count - 1].tl.face_bytes;
    if (cube) require(face_stride == 128, "BC3 8x8 mip pyramid uses 16-byte blocks and 128-byte face stride");
    std::vector<u8> source(face_stride * faces, 0xEE);
    for (u32 f = 0; f < faces; ++f) for (u32 m = 0; m < count; ++m) {
        auto& level = levels[m];
        u8* bytes = source.data() + f * face_stride + level.offset;
        if (cube) {
            for (u32 b = 0; b < level.tl.face_bytes; ++b)
                bytes[b] = (u8)(f * 31 + m * 7 + b);
        } else {
            for (u32 p = 0; p < level.w * level.h; ++p) {
                bytes[p * 4] = 255; bytes[p * 4 + 1] = (u8)(20 + m * 60);
                bytes[p * 4 + 2] = (u8)(30 + m); bytes[p * 4 + 3] = (u8)(40 + m);
            }
        }
    }
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = desc.Height = width;
    desc.DepthOrArraySize = (UINT16)faces;
    desc.MipLevels = (UINT16)count;
    desc.Format = cube ? DXGI_FORMAT_BC3_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(faces * count);
    UINT64 upload_size = 0;
    device->GetCopyableFootprints(&desc, 0, faces * count, 0, footprints.data(), nullptr, nullptr, &upload_size);
    std::vector<u8> mapped((size_t)upload_size, 0xCD);
    vp_decode_texture_subresources(mapped.data(), source.data(), levels, count, faces,
                                  face_stride, footprints.data(), 0);
    for (u32 f = 0; f < faces; ++f) for (u32 m = 0; m < count; ++m) {
        auto& level = levels[m];
        auto& fp = footprints[f * count + m];
        require(fp.Offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT == 0, "subresource offset is GPU aligned");
        require(fp.Footprint.RowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT == 0, "row pitch is GPU aligned");
        for (u32 y = 0; y < level.tl.rows; ++y) {
            auto* row = mapped.data() + fp.Offset + y * fp.Footprint.RowPitch;
            if (cube) {
                auto* expected = source.data() + f * face_stride + level.offset + y * level.tl.row_bytes;
                require(!memcmp(row, expected, level.tl.row_bytes), "every BC3 cube face/mip preserves its own blocks");
            } else {
                for (u32 x = 0; x < level.w; ++x) {
                    require(row[x * 4] == 20 + m * 60 && row[x * 4 + 1] == 30 + m &&
                            row[x * 4 + 2] == 40 + m && row[x * 4 + 3] == 255,
                            "every ARGB mip reaches its own RGBA upload footprint");
                }
            }
        }
    }
}

int main()
{
    ID3D12Device* device = nullptr;
    require(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))),
            "create D3D12 device for real upload footprints");
    verify(device, false);
    verify(device, true);
    device->Release();
    puts("PASS: actual mip decode fills aligned GPU footprints for ARGB and BC3 cube chains");
}
