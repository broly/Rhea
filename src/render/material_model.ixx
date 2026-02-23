export module render:material_model;

import name;
import rhobject;
import <map>;
import <vector>;
import <string>;
#include "common/reflect_macros.h"
#include "object/object_reflection_macro.h"
#include "common/type_macros.h"
import <bit>;

#include "common/assertion_macros.h"


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


export constexpr uint8_t ShaderStage_index(ShaderStage stage)
{
    return std::bit_width(static_cast<uint16_t>(stage)) - 1;
}

export constexpr uint8_t MAX_STAGES = ShaderStage_index(ShaderStage::all) + 1;



export struct MatModel_Permutations
{
    std::map<Name, std::string> flags;
    std::map<Name, std::map<Name, Name>> enums;
};
REFLECT_STRUCT(MatModel_Permutations,
    flags, enums);


export struct MatModel_PushConstant
{
    Name name;
    Name type;
    std::vector<ShaderStage> stages;
};
REFLECT_STRUCT(MatModel_PushConstant,
    name, type, stages);



export struct MatModel_LayoutAttributeInfo
{
    Name name;
    Name definition;
    Name variable;
};
REFLECT_STRUCT(MatModel_LayoutAttributeInfo,
    name, definition, variable);

export struct MatModel_AttributesLayout
{
    Name vertex_type;
    std::vector<MatModel_LayoutAttributeInfo> attributes;
};
REFLECT_STRUCT(MatModel_AttributesLayout,
    vertex_type, attributes);



export enum RBBufferTopology
{
    point_list,
    line_list,
    line_strip,
    triangle_list,
    triangle_strip,
    triangle_fan,
    line_list_with_adjacency,
    line_strip_with_adjacency,
    triangle_list_with_adjacency,
    triangle_strip_with_adjacency,
    patch_list,
};
REFLECT_ENUM(RBBufferTopology, 
    point_list, line_list, line_strip, triangle_list, triangle_strip, triangle_fan, line_list_with_adjacency, 
    line_strip_with_adjacency, triangle_list_with_adjacency, triangle_strip_with_adjacency, patch_list)


export enum class MaterialParamType
{
    definition,
    uniform,
    sampler,
    Float,
    vec2,
    vec3,
    vec4,
};
REFLECT_ENUM(MaterialParamType,
    definition, uniform, sampler, Float, vec2, vec4, vec4);



export struct MatModel_Parameter
{
    MaterialParamType type;
    std::optional<Name> definition;
    std::optional<Name> variable;
    std::optional<Name> sampler;
    std::optional<Name> ubo;
    std::optional<Name> binding;
    std::optional<Name> storage;
    
    std::optional<size_t> size; // not serializable
    
    bool is_descriptor() const
    {
        return type == MaterialParamType::uniform || type == MaterialParamType::sampler;
    }
    
    void validate();
};
REFLECT_STRUCT(MatModel_Parameter,
    type, variable, ubo, binding, storage, sampler, definition);


export struct MatModel_PassStage
{
    Name shader;
    std::set<Name> resources;
};
REFLECT_STRUCT(MatModel_PassStage,
    shader, resources);


export enum class ResourceUsageType
{
    frame,      // per-frame (camera, per-frame UBOs)
    persistent  // materials, textures etc.
};
REFLECT_ENUM(ResourceUsageType,
    frame, persistent);

export struct ResourceUsage
{
    ResourceUsageType type;
    
    ENUM_WRAPPER_STRUCT(ResourceUsage, ResourceUsageType, type);
    
    bool is_frame_based() const
    {
        return type == ResourceUsageType::frame;
    }
    
    uint32_t frame_index(uint32_t frame) const
    {
        return type == ResourceUsageType::frame ? frame : 0;
    }
};

export struct MatModel_Resource
{
    Name name;
    ResourceUsageType usage;
    std::map<Name, MatModel_Parameter> parameters;
    Name set;
    
    uint32_t set_index; // not serialized
};
REFLECT_STRUCT(MatModel_Resource,
    name, usage, parameters, set);


export enum class MatModel_WriteMaskEnum
{
    R, G, B, A
};
REFLECT_ENUM(MatModel_WriteMaskEnum,
    R, G, B, A);

export struct MatModel_ColorAttachmentInfo
{
    std::set<MatModel_WriteMaskEnum> write_mask;
    bool translucent;
};
REFLECT_STRUCT(MatModel_ColorAttachmentInfo,
    write_mask, translucent);


export struct MatModel_Pass
{
    Name name;
    std::string requirements;
    std::vector<MatModel_AttributesLayout> vertex_layouts;
    std::map<ShaderStage, MatModel_PassStage> stages;
    bool depth_test;
    bool depth_write;
    std::vector<MatModel_PushConstant> push_constants;
    std::vector<MatModel_ColorAttachmentInfo> color_attachments;
    CullMode cull_mode;
    FrontFace front_face;
    std::optional<bool> no_color_attachments;
    CompareOp compare_op;
    std::optional<DepthBiasInfo> depth_bias;
    RBBufferTopology topology;
};
REFLECT_STRUCT(MatModel_Pass,
    name, requirements, stages, depth_test, push_constants, depth_write, no_color_attachments, color_attachments, topology, cull_mode, front_face, compare_op, depth_bias, vertex_layouts);




export class MaterialModel : public RhObject
{
public:
    Name model_name;
    std::map<Name, std::vector<Name>> enums;
    MatModel_Permutations permutations;
    std::vector<MatModel_Pass> passes;
    
    std::optional<Name> material_resource;
    
    Name set;
    
    const MatModel_Pass* get_pass_info(Name pass_name) const;
    
    void on_serialize(const SerializationContext& context) override;
};
REFLECT_OBJECT_FIELDS(MaterialModel, RhObject,
                      model_name, enums, permutations, passes, material_resource);




export class RenderResourceInfo : public RhObject
{
public:
    void on_serialize(const SerializationContext& context) override;
    
    void update_set(uint32_t set_index)
    {
        resource.set_index = set_index;
    }
    
    MatModel_Resource resource;
    
};
REFLECT_OBJECT_FIELDS(RenderResourceInfo, RhObject,
    resource);