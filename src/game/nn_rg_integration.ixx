export module game:nn_denoiser_passes;

import name;
import :nn_pipeline;
import :nn_weights;
import glm;
import rhmath;
import <vector>;
import <map>;
import <filesystem>;
import :nn_ubo;
#include "common/assertion_macros.h"
#include "common/reflect_macros.h"



export struct NNDenoiserState
{
    std::shared_ptr<NNPipeline>       pipeline; 
    NNWeightsIndex   weights_index;

    // GPU resources
    std::vector<RGTextureHandle>              activation_textures;
    
    // NOTE: Weights uploaded as flat SSBOs (see: u_nn_pw_weights / u_nn_dw_weights / u_nn_biases)

    std::map<Name, std::shared_ptr<PipelineFamily>> families;

    std::vector<NNPassIndicesSSBO> pass_ubo_templates;

    int32_t layout_dw_base = 0;
    int32_t layout_bias_base = 0;

    bool initialized = false;

    // Pointwise back-end switch for enc/dec/bottleneck/up: false = scalar dot products, true = coopmat/GEMM on the 
    // tensor cores. Each value is a distinct ShaderKey -> its own PSO, so flipping it gives a clean A/B. Flipping at
    // runtime has to invalidate cached PSOs (see set_coopmat_gemm / on_pso_built).
    bool use_coopmat_gemm = true;
    
    RenderResource* resource;
    
    std::map<uint32_t, PipelineObject*> pso = {};
};

namespace nn_denoiser
{
    NNPassIndicesSSBO make_ubo_from_pass_indices(const NNPassIndicesData& pi);
    
    export void on_pso_built(NNDenoiserState& state);

    // Flip the pointwise back-end for benchmarking. Invalidates cached PSOs so the next frame rebuilds with the new 
    // COOPMAT permutation. Returns true if the value changed.
    export bool set_coopmat_gemm(NNDenoiserState& state, bool enable);
    
    export void load_nn_denoiser_schemas(NNDenoiserState& state);

    export void allocate_nn_gpu_resources(
        NNDenoiserState& state,
        RenderGraph& rg,
        Renderer& renderer,
        RenderBackend& backend,
        const Extent& swapchain_extent);
    
    export void prepare_resources(
        NNDenoiserState& state, RenderGraphContext& ctx);

    export void add_nn_denoiser_passes(NNDenoiserState& state, RenderGraph& rg, Renderer& renderer);
}
