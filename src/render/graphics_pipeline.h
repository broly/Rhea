#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "handle_types.h"

enum class VertexLayout
{
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
    Sampler,
    Texture,
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

struct GraphicsPipelineDesc
{
    std::string vertex_shader;
    std::string fragment_shader;
    VertexLayout vertex_layout;
    PipelineLayoutDesc layout;
    bool depth_test;
    
};


struct FrameResources
{
    std::unordered_map<uint32_t, RBDescriptorSet> descriptor_sets;
};


using FrameIndex = uint32_t;