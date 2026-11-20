# The SDK 1.1.4 legacy API was looking up preparation resources on the
# upscaler even in interpolation-only mode, when that upscaler does not exist.
set(_fsr3_source src/components/fsr3/ffx_fsr3.cpp)
fidelityfx_replace("${_fsr3_source}"
    "    // validate that all callbacks are set for the backend interfaces\n    if (contextPrivate->interpolationOnly)"
    "    contextPrivate->interpolationOnly = interpolationOnly;\n\n    // Validate that all callbacks are set for the backend interfaces.\n    if (contextPrivate->interpolationOnly)")

fidelityfx_replace("${_fsr3_source}" [[
    // set up FSR3Upscaler resources
    if (!contextPrivate->interpolationOnly)
    {
        FfxFsr3UpscalerSharedResourceDescriptions fs3UpscalerResourceDescs = {};
        FFX_VALIDATE(ffxFsr3UpscalerGetSharedResourceDescriptions(&contextPrivate->upscalerContext, &fs3UpscalerResourceDescs));
]] [[
    // Prepare resources are shared between the active producer and interpolation.
    {
        FfxFsr3UpscalerSharedResourceDescriptions fs3UpscalerResourceDescs = {};
        if (interpolationOnly)
        {
            FfxFrameInterpolationSharedResourceDescriptions fiResourceDescs = {};
            FFX_VALIDATE(ffxFrameInterpolationGetSharedResourceDescriptions(&contextPrivate->fiContext, &fiResourceDescs));
            fs3UpscalerResourceDescs.dilatedDepth = fiResourceDescs.dilatedDepth;
            fs3UpscalerResourceDescs.dilatedMotionVectors = fiResourceDescs.dilatedMotionVectors;
            fs3UpscalerResourceDescs.reconstructedPrevNearestDepth = fiResourceDescs.reconstructedPrevNearestDepth;
        }
        else
        {
            FFX_VALIDATE(ffxFsr3UpscalerGetSharedResourceDescriptions(&contextPrivate->upscalerContext, &fs3UpscalerResourceDescs));
        }
]])

# A failed callback still has to order the real frame after the game's fence.
# The presentation queue waits for the interpolation fence even when no
# generated frame is queued. Signal it behind the interpolation queue's wait.
fidelityfx_replace(src/backends/dx12/FrameInterpolationSwapchain/FrameInterpolationSwapchainDX12.cpp [[
        else
        {
            interpolationCommandList->drop();
        }
]] [[
        else
        {
            interpolationCommandList->drop();
            desc.numInterpolatedFrames = 0;
            presentInfo.interpolationQueue->Signal(presentInfo.interpolationFence, ++interpolationFenceValue);
        }
]])
