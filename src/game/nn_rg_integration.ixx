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
    std::vector<RGTextureHandle>              pw_weight_textures;
    std::vector<RGTextureHandle>              dw_weight_textures;
    std::vector<RGTextureHandle>              bias_textures;

    std::map<Name, std::shared_ptr<PipelineFamily>> families;

    std::vector<NNPassIndicesSSBO> pass_ubo_templates;

    bool initialized = false;
    
    RenderResource* resource;
};

namespace nn_denoiser
{
    NNPassIndicesSSBO make_ubo_from_pass_indices(const NNPassIndicesData& pi);


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
    
    // export void init_and_add_nn_passes(
    //     NNDenoiserState& state,
    //     RenderGraph& rg,
    //     Renderer& renderer,
    //     RenderBackend& backend,
    //     const Extent& swapchain_extent);
}
