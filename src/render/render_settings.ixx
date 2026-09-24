export module render:settings;

// Compile-time render feature switches.
export namespace render_settings
{
    // Hardware ray tracing: RTXGI + SVGF passes, TLAS/BLAS builds.
    // When disabled, the lighting pass takes ambient light from the reflection capture IBL.
    inline constexpr bool enable_raytracing = false;

    // Neural denoiser of the RTXGI signal (requires ray tracing).
    inline constexpr bool enable_nn_denoiser = false;

    static_assert(!enable_nn_denoiser || enable_raytracing, "NN denoiser needs ray tracing");
}
