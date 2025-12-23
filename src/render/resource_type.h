#pragma once

enum class RenderResource {
    SwapchainColor,

    Depth,

    GBuffer_Albedo,
    GBuffer_Normal,
    GBuffer_MetallicRoughness,
    GBuffer_Emissive,

    ShadowMap,
    SSAO,
    Lighting,
};