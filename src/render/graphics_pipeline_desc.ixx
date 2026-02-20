export module render:graphics_pipeline_desc;
import <string>;
import <unordered_map>;
import <vector>;
import <memory>;
import <bit>;
import :handle_types;
import :rg_types;
import <variant>;

#include "common/assertion_macros.h"
#include "common/reflect_macros.h"
#include "common/type_macros.h"
import <map>;
import name;

export class RenderResource;

export
{
    
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
        uint32_t binding_index; // index of binding in shader
        DescriptorType type; // UniformBuffer, Sampler, Texture
        ShaderStage stages; // VS | FS
        uint32_t count = 1; // arrays...
        uint32_t size = 0;
        Name name;
        RBSampler sampler;
    };

    struct PushConstantRange
    {
        ShaderStage stages;
        uint32_t offset;
        uint32_t size;
    };

    
    struct ResourceBinding
    {
        MatModel_Parameter parameter;
        std::optional<uint32_t> binding_index;
        std::optional<RBSampler> sampler;
        ShaderStage stages;
        
        bool is_descriptor() const
        {
            return parameter.is_descriptor();
        }
    };
    
    struct DescriptorSetLayoutDesc
    {
        uint32_t set_index;
        std::vector<ResourceBinding> bindings;
        Name debug_name;

        std::tuple<uint32_t, const ResourceBinding&> get_binding(Name name) const
        {
            for (uint32_t index = 0; auto& binding : bindings)
            {
                if (*binding.parameter.variable == name)
                    return {index, binding};
                index++;
            }
            unreachable("Could not find binding named %s", name.to_string().c_str());
        }
    };

    struct PipelineRenderTargetDesc
    {
        uint32_t color_attachment_count = 0;
        bool has_depth = false;
    };

    struct VertexComponentLayoutData_DEPRECATED
    {
        uint8_t location;
        uint16_t offset;
        uint8_t binding;
        uint8_t num_components;
        uint8_t component_size;
    };

    struct VertexLayoutData
    {
        uint32_t binding_index;
        size_t stride;
        std::vector<VertexAttributeInfo> attributes;
    };

    struct VertexLayout
    {
        std::vector<VertexLayoutData> layouts;
    };
    
    
    struct GraphicsPipelineResourceInfo
    {
        Name name;
        RenderResource* resource;
        uint16_t set;
        std::vector<ResourceBinding> resource_variable_bindings;
    };

    struct PipelineLayoutDesc
    {
        Name pass;
        VertexLayout vertex_layout;
        std::vector<GraphicsPipelineResourceInfo> resources;
        std::vector<PushConstantRange> push_constants;
        RBPipelineLayout pipeline_layout;
        
        const GraphicsPipelineResourceInfo* get_graphics_pipeline_resource_info(RenderResource* rr) const
        {
            for (const auto& info : resources)
                if (info.resource == rr)
                    return &info;
            return nullptr;
        }
    };
    


    struct GraphicsPipelineStage
    {
        ShaderStage stage;
        std::string shader;
        std::optional<std::string> compiled_shader;
    };

    struct ShaderFeatureEnum
    {
        Name name;
        std::vector<Name> members;
    };

    struct ShaderFeatureFlag
    {
        Name name;
    };

    using ShaderFeature = std::variant<ShaderFeatureEnum, ShaderFeatureFlag>;

    struct GraphicsPipelineDesc
    {
        Name pass_name;
        uint64_t permutation_value;
        std::vector<GraphicsPipelineStage> stages;
        PipelineLayoutDesc layout;
        bool depth_test;
        bool depth_write;
        bool no_color_attachments;
        CullMode cull_mode;
        FrontFace front_face;
        CompareOp compare_op;
        DepthBiasInfo depth_bias;
        RBBufferTopology topology;
        uint32_t num_color_passes;
        std::vector<MatModel_ColorAttachmentInfo> color_attachments;
        

        PipelineRenderTargetDesc rt_compat;
        
        
    };


    struct FrameResources
    {
        std::unordered_map<uint32_t, RBDescriptorSet> descriptor_sets;
    };


    using FrameIndex = uint32_t;
}
