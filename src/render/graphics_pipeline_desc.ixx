export module render:graphics_pipeline_desc;
import <string>;
import <unordered_map>;
import <vector>;

import :handle_types;
import :rg_types;
#include "common/type_macros.h"

export
{

    enum ShaderStage
    {
        ss_Vertex = 0x1,
        ss_Fragment = 0x2,
    };
    ENUM_MASK_OPS(ShaderStage);

    enum class DescriptorType
    {
        UniformBuffer,
        StorageBuffer,
        Sampler,
        SampledImage,
        CombinedImageSampler,
    };

    const char* to_string(DescriptorType e)
    {
        switch (e)
        {
        case DescriptorType::UniformBuffer: return "UniformBuffer";
        case DescriptorType::StorageBuffer: return "StorageBuffer";
        case DescriptorType::Sampler: return "Sampler";
        case DescriptorType::SampledImage: return "SampledImage";
        case DescriptorType::CombinedImageSampler: return "CombinedImageSampler";
        default: return "unknown";
        }
    }

    struct DescriptorBinding
    {
        uint32_t binding;              // number of binding in shader
        DescriptorType type;           // UniformBuffer, Sampler, Texture
        ShaderStage stages;            // VS | FS
        uint32_t count = 1;            // arrays...
    };

    struct PushConstantRange
    {
        ShaderStage stages;
        uint32_t offset;
        uint32_t size;
    };

    struct DescriptorSetLayoutDesc
    {
        uint32_t set_index;
        std::vector<DescriptorBinding> bindings;
        std::string debug_name;
    };

    struct PipelineLayoutDesc
    {
        std::vector<RBDescriptorSetLayout> sets;
        std::vector<PushConstantRange> push_constants;
    };

    struct PipelineRenderTargetDesc
    {
        uint32_t color_attachment_count = 0;
        bool has_depth = false;
    };
    
    struct VertexComponentLayoutData
    {
        uint8_t location;
        uint16_t offset;
        uint8_t binding;
        uint8_t num_components;
        uint8_t component_size;
    };
    
    struct VertexLayoutData
    {
        size_t stride;
        std::vector<VertexComponentLayoutData> data;
    };

    struct GraphicsPipelineDesc
    {
        std::string vertex_shader;
        std::string fragment_shader;
        VertexLayoutData vertex_layout;
        PipelineLayoutDesc layout;
        bool depth_test;
        
        PipelineRenderTargetDesc rt_compat;
    };


    struct FrameResources
    {
        std::unordered_map<uint32_t, RBDescriptorSet> descriptor_sets;
    };


    using FrameIndex = uint32_t;
}
