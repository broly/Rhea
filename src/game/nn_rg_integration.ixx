export module game:nn_denoiser_passes;

import name;
import :nn_pipeline;
import :nn_weights;
import glm;
import rhmath;
import <vector>;
import <map>;
import <filesystem>;
#include "common/assertion_macros.h"
#include "common/reflect_macros.h"


export struct NNPassIndicesUBO
{
    glm::ivec2  uOutSize;
    int32_t     _pad0[2];          // align to vec4

    int32_t     uActIn[64];
    int32_t     uActOut[64];
    int32_t     uActRes[64];

    int32_t     uPwIdx;
    int32_t     uDwIdx;
    int32_t     uBiasIdx;
    int32_t     _pad1;
};

REFLECT_STRUCT(NNPassIndicesUBO,
    uOutSize, uActIn, uActOut, uActRes, uPwIdx, uDwIdx, uBiasIdx);


export struct NNDenoiserState
{
    std::shared_ptr<NNPipeline>       pipeline; 
    NNWeightsIndex   weights_index;

    // GPU resources
    std::vector<RGTextureHandle>            activation_textures;
    std::vector<RBImageHandle>              pw_weight_textures;
    std::vector<RBImageHandle>              dw_weight_textures;
    std::vector<RBImageHandle>              bias_textures;

    std::map<Name, std::shared_ptr<PipelineFamily>> families;

    std::vector<NNPassIndicesUBO> pass_ubo_templates;

    bool initialized = false;
};

namespace nn_denoiser
{
    NNPassIndicesUBO make_ubo_from_pass_indices(const NNPassIndicesData& pi);


    void load_nn_denoiser_schemas(NNDenoiserState& state);

    void allocate_nn_gpu_resources(
        NNDenoiserState& state,
        RenderGraph& rg,
        Renderer& renderer,
        RenderBackend& backend,
        const Extent& swapchain_extent);

    void add_nn_denoiser_passes(NNDenoiserState& state, RenderGraph& rg, Renderer& renderer);
    
    export void init_and_add_nn_passes(
        NNDenoiserState& state,
        RenderGraph& rg,
        Renderer& renderer,
        RenderBackend& backend,
        const Extent& swapchain_extent);
}
