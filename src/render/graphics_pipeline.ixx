export module render:graphics_pipeline;
import <string>;
import <unordered_map>;
import <vector>;

import :handle_types;
import :rg_types;

export
{

    enum class VertexLayout
    {
        None,
        Position,
        PositionNormal,
        PositionNormalUV,
        PositionNormalTangentUV,
    };

    enum ShaderStage
    {
        ss_Vertex = 0x1,
        ss_Fragment = 0x2,
    };

    enum class DescriptorType
    {
        UniformBuffer,
        StorageBuffer,
        Sampler,
        SampledImage,
        CombinedImageSampler,
    };

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

    struct GraphicsPipelineDesc
    {
        std::string vertex_shader;
        std::string fragment_shader;
        VertexLayout vertex_layout;
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