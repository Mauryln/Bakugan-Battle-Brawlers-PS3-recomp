#include "fidelityfx_boot_report.h"

#include <FidelityFX/host/backends/dx12/ffx_dx12.h>
#include <FidelityFX/host/ffx_fsr3.h>

namespace bakugan {
void write_fidelityfx_boot_report(std::FILE *file) {
    std::fprintf(file, "ffx_fsr3_version=%u\n", ffxFsr3GetEffectVersion());
    std::fprintf(file, "ffx_opticalflow_version=%u\n", ffxOpticalflowGetEffectVersion());
    std::fprintf(file, "ffx_frameinterpolation_version=%u\n", ffxFrameInterpolationGetEffectVersion());
    std::fprintf(file, "ffx_dx12_scratch_bytes=%zu\n", ffxGetScratchMemorySizeDX12(FFX_FSR3_CONTEXT_COUNT));
}
}
