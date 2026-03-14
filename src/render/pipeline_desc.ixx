export module render:pipeline_desc;
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
    struct PushConstantRange
    {
        Mask<ShaderStage> stages;
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
    
    
    struct PipelineResourceInfo
    {
        Name name;
        RenderResource* resource;
        std::vector<ResourceBinding> resource_variable_bindings;
    };

    struct PipelineLayoutDesc
    {
        Name pass;
        std::vector<PipelineResourceInfo> resources;
        std::vector<PushConstantRange> push_constants;
    };


    struct PipelineStage
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
    
    struct PipelineCreateDesc_Base
    {
        Name pass_name;
        uint64_t permutation_value;
        PipelineLayoutDesc layout;
    };

    struct PipelineCreateDesc_Graphics : public PipelineCreateDesc_Base
    {
        bool depth_test;
        bool depth_write;
        bool no_color_attachments;
        CullMode cull_mode;
        FrontFace front_face;
        CompareOp compare_op;
        DepthBiasInfo depth_bias;
        RBBufferTopology topology;
        std::vector<MatModel_ColorAttachmentInfo> color_attachments;
        VertexLayout vertex_layout;
        std::vector<PipelineStage> stages;
    };
    
    
    struct PipelineCreateDesc_Compute : public PipelineCreateDesc_Base
    {
        PipelineStage compute_stage;
    };
    
    
    struct RayTracingShaderStage
    {
        ShaderStage stage;
        std::string compiled_shader;
        std::string shader_source;
    };

    struct RayTracingShaderGroupDesc
    {
        RayTracingGroupType type;

        int general_shader = -1;
        int closest_hit_shader = -1;
        int any_hit_shader = -1;
        int intersection_shader = -1;
    };

    struct PipelineCreateDesc_RayTrace : public PipelineCreateDesc_Base
    {
        uint32_t max_recursion_depth = 1;

        std::vector<RayTracingShaderStage> stages;
        std::vector<RayTracingShaderGroupDesc> groups;

        std::vector<uint32_t> sbt_raygen;
        std::vector<uint32_t> sbt_miss;
        std::vector<uint32_t> sbt_hit;
        std::vector<uint32_t> sbt_callable;
    };

    struct FrameResources
    {
        std::unordered_map<uint32_t, RBDescriptorSet> descriptor_sets;
    };


    using FrameIndex = uint32_t;
}
