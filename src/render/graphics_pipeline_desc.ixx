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


export enum class ShaderStage : uint16_t
{
    none = 0x0,
    vertex = 0x1,
    fragment = 0x2,
    reserved2 = 0x4,
    reserved3 = 0x8,
    reserved4 = 0x10,
    reserved5 = 0x20,
    reserved6 = 0x40,
    reserved7 = 0x60,
    all = vertex | fragment,
};
REFLECT_ENUM(ShaderStage,
    none, vertex, fragment, reserved2, reserved3, reserved4, reserved5, reserved6, reserved7);
ENUM_MASK_OPS(ShaderStage, export);

export ShaderStage make_shader_stages_mask(const std::vector<ShaderStage>& stages)
{
    ShaderStage result = ShaderStage::none;
    for (auto& stage : stages)
        result |= stage;
    return result;
}

export enum class CullMode
{
    none, front, back, both
};
REFLECT_ENUM(CullMode,
    none, front, back, both);

struct DepthBiasInfo
{
    bool enable = false;
    float constant_factor = 0.0;
    float clamp = 0.0;
    float slope_factor = 0.0;
};
REFLECT_STRUCT(DepthBiasInfo,
    enable, constant_factor, clamp, slope_factor);

export enum class CompareOp 
{
    never = 0,
    less = 1,
    equal = 2,
    less_or_equal = 3,
    greater = 4,
    not_equal = 5,
    greater_or_equal = 6,
    always = 7,
};
REFLECT_ENUM(CompareOp,
    never, less, equal, less_or_equal, greater, not_equal, greater_or_equal, always);


export enum class FrontFace
{
    CW, CCW,
};
REFLECT_ENUM(FrontFace,
    CW, CCW);

struct VertexAttributeInfo
{
    Name variable_name;
    uint16_t location;
    uint32_t offset;
};

export
{
    constexpr uint8_t ShaderStage_index(ShaderStage stage)
    {
        return std::bit_width(static_cast<uint16_t>(stage)) - 1;
    }

    constexpr uint8_t MAX_STAGES = ShaderStage_index(ShaderStage::all) + 1;

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

    struct DescriptorSetLayoutDesc
    {
        uint32_t set_index;
        std::vector<DescriptorBinding> bindings;
        Name debug_name;

        std::tuple<uint32_t, const DescriptorBinding&> get_binding(Name name) const
        {
            for (uint32_t index = 0; auto& binding : bindings)
            {
                if (binding.name == name)
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
        RenderResource* resource;
        uint16_t set;
        std::vector<uint32_t> resource_variable_bindings;
    };

    struct PipelineLayoutDesc
    {
        VertexLayout vertex_layout;
        std::vector<GraphicsPipelineResourceInfo> resources;
        std::vector<PushConstantRange> push_constants;
        
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
        bool is_translucent;
        bool no_color_attachments;
        CullMode cull_mode;
        FrontFace front_face;
        CompareOp compare_op;
        DepthBiasInfo depth_bias;
        

        PipelineRenderTargetDesc rt_compat;
        
        
    };


    struct FrameResources
    {
        std::unordered_map<uint32_t, RBDescriptorSet> descriptor_sets;
    };


    using FrameIndex = uint32_t;
}
