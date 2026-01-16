export module render:graphics_pipeline_desc;
import <string>;
import <unordered_map>;
import <vector>;
import <memory>;
import <bit>;
import :handle_types;
import :rg_types;
#include "common/assertion_macros.h"
#include "common/type_macros.h"
import name;

export
{

    enum class ShaderStage : uint16_t
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

    const char* to_string(ShaderStage e)
    {
        switch (e)
        {
        case ShaderStage::none: return "none";
        case ShaderStage::vertex: return "vertex";
        case ShaderStage::fragment: return "fragment";
        case ShaderStage::reserved2: return "reserved2";
        case ShaderStage::reserved3: return "reserved3";
        case ShaderStage::reserved4: return "reserved4";
        case ShaderStage::reserved5: return "reserved5";
        case ShaderStage::reserved6: return "reserved6";
        case ShaderStage::reserved7: return "reserved7";
        case ShaderStage::all: return "all";
        default: return "unknown";
        }
    }
    ENUM_MASK_OPS(ShaderStage);
    
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
        uint32_t binding_index;              // index of binding in shader
        DescriptorType type;           // UniformBuffer, Sampler, Texture
        ShaderStage stages;            // VS | FS
        uint32_t count = 1;            // arrays...
        uint32_t size = 0;
        Name name;
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
            unreachable("Could not find binding named %s", name);
        }
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
    
    struct VertexComponentLayoutData_DEPRECATED
    {
        uint8_t location;
        uint16_t offset;
        uint8_t binding;
        uint8_t num_components;
        uint8_t component_size;
    };
    
    struct VertexAttributeInfo
    {
        const char* variable_name;
        uint16_t location;
        uint32_t offset;
    };
    
    struct VertexLayoutData
    {
        uint32_t binding_index;
        size_t stride;
        std::vector<VertexAttributeInfo> attributes;
    };
    
    
    struct GraphicsPipelineStage
    {
        ShaderStage stage;
        std::string shader;
        std::vector<VertexLayoutData> vertex_layouts;
    };
    
    struct GraphicsPipelineDesc
    {
        std::vector<GraphicsPipelineStage> stages;
        // std::string vertex_shader;
        // std::string fragment_shader;
        // VertexLayoutData vertex_layout;
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
