export module render:material_model;

import name;
import rhobject;
import <map>;
import <vector>;
import <string>;
import <variant>;
#include "common/reflect_macros.h"
#include "object/object_reflection_macro.h"
#include "common/type_macros.h"
import enum_helpers;
import <bit>;
import glm;

#include "common/assertion_macros.h"


export enum class ShaderStage : uint16_t
{
    none = 0x0,
    // Graphics pipeline
    vertex =                    1 << 0,
    fragment =                  1 << 1,
    geometry =                  1 << 2,
    tesselation_control =       1 << 3,
    tesselation_evaluation =    1 << 4,
    
    // Compute pipeline
    compute =                   1 << 5,
    
    // Raytrace pipeline
    rtx_raygen =                1 << 6,
    rtx_any_hit =               1 << 7,
    rtx_closest_hit =           1 << 8,
    rtx_miss =                  1 << 9,
    rtx_intersection =          1 << 10,
    rtx_callable =              1 << 11,
    
    
    all_graphics = vertex | fragment | geometry | tesselation_control | tesselation_evaluation,
    all_compute = compute,
    all_rtx = rtx_raygen | rtx_any_hit | rtx_closest_hit | rtx_miss | rtx_intersection | rtx_callable,
    
    all = all_graphics | all_compute | all_rtx,
    
};
REFLECT_ENUM(ShaderStage,
    none, 
    vertex, fragment, geometry,
    tesselation_control, tesselation_evaluation,
    compute,
    rtx_raygen, rtx_any_hit, rtx_closest_hit, rtx_miss, rtx_intersection, rtx_callable);
ENUM_MASK_OPS(ShaderStage, export);

export bool is_graphics_stage(ShaderStage stage)
{
    return stage != ShaderStage::none && 
           (stage & ~ShaderStage::all_graphics) == ShaderStage::none;
}

export bool is_compute_stage(ShaderStage stage)
{
    return stage != ShaderStage::none && 
           (stage & ~ShaderStage::all_compute) == ShaderStage::none;
}

export bool is_rtx_stage(ShaderStage stage)
{
    return stage != ShaderStage::none && 
           (stage & ~ShaderStage::all_rtx) == ShaderStage::none;
}

export Mask<ShaderStage> make_shader_stages_mask(const std::vector<ShaderStage>& stages)
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
    Mask<ShaderStage> stages;
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
    // hard macro (fixed parameter for permutations)
    definition,
    
    // uniform buffer parameter (could be set of sub-parameters)
    uniform,
    
    // sampled image parameter
    sampler,
    
    // just image parameter (could be used for write in RTX)
    image,
    
    // TLAS parameter
    tlas,
    
    // SSBO parameter
    ssbo,
    
    // Sub-parameters of uniform buffer:
    
    Float,
    vec2,
    vec3,
    vec4,
};
REFLECT_ENUM(MaterialParamType,
    definition, uniform, sampler, image, ssbo, tlas, Float, vec2, vec4, vec4);



export struct MatModel_Parameter
{
    MaterialParamType type;
    
    // 
    std::optional<Name> definition;
    
    // Name of shader value (can be changed via code)
    std::optional<Name> variable;
    
    // Name of sampler (filtering, etc.)
    std::optional<Name> sampler;
    
    // Name of C++ UBO class (used by reflection to connect shader and C++ structs)
    std::optional<Name> ubo;
    
    // binding definition (macro for binding parameter of layout)
    std::optional<Name> binding;
    
    // If this parameter is subparameter of UBO, this should be provided as name of parameter
    std::optional<Name> storage;
    
    // used in ssbo (initial buffer size)
    std::optional<size_t> initial_buffer_size;
    
    std::optional<size_t> initial_array_size;
    
    std::optional<size_t> ubo_size; // not serializable
    
    bool is_descriptor() const
    {
        return type == MaterialParamType::uniform || type == MaterialParamType::sampler || 
            type == MaterialParamType::image || type == MaterialParamType::ssbo || type == MaterialParamType::tlas;
    }
    
    void validate();
};
REFLECT_STRUCT(MatModel_Parameter,
    type, variable, ubo, binding, storage, sampler, definition, initial_buffer_size, initial_array_size);


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
    
    uint32_t frame_index(std::optional<uint32_t> frame) const
    {
        if (is_frame_based())
        {
            checkf(frame.has_value(), "frame-based resource usage must handle frame");
            return *frame;
        }
        return 0;
    }
};

export struct MatModel_Resource
{
    Name name;
    ResourceUsageType usage;
    std::map<Name, MatModel_Parameter> parameters;
    Name set;
    Mask<ShaderStage> allowed_stages;
    
    uint32_t set_index; // not serialized
};
REFLECT_STRUCT(MatModel_Resource,
    name, usage, parameters, set, allowed_stages);


export struct PipelineInfoShaderStageInfo
{
    Name stage_name;
    ShaderStage stage;
    std::set<Name> resources;
};
    

export struct PipelineInfo
{
    Name pass;
    std::vector<MatModel_PushConstant> push_constants;
    
    virtual ~PipelineInfo() {}
    virtual std::vector<PipelineInfoShaderStageInfo> get_stages() const pure;
};
REFLECT_STRUCT(PipelineInfo,
    pass, push_constants);



/************************************************************************
 * GRAPHICS PIPELINE
 ***********************************************************************/


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


export struct PipelineInfo_Graphics : public PipelineInfo
{
    std::string requirements;
    std::map<ShaderStage, MatModel_PassStage> stages;
    std::vector<MatModel_AttributesLayout> vertex_layouts;
    bool depth_test;
    bool depth_write;
    std::vector<MatModel_ColorAttachmentInfo> color_attachments;
    CullMode cull_mode;
    FrontFace front_face;
    std::optional<bool> no_color_attachments;
    CompareOp compare_op;
    std::optional<DepthBiasInfo> depth_bias;
    RBBufferTopology topology;
    
    std::vector<PipelineInfoShaderStageInfo> get_stages() const override 
    {
        std::vector<PipelineInfoShaderStageInfo> result;
        for (auto& [stage, info] : stages)
        {
            PipelineInfoShaderStageInfo stage_info;
            stage_info.stage_name = reflect::enum_name(stage);
            stage_info.stage = stage;
            stage_info.resources = info.resources;
            result.push_back(stage_info);
        }
        return result;
    }
};
REFLECT_STRUCT_DERIVED(PipelineInfo_Graphics, PipelineInfo,
    requirements, depth_test, depth_write, no_color_attachments, stages, color_attachments, topology, cull_mode, front_face, compare_op, depth_bias, vertex_layouts);



/************************************************************************
 * COMPUTE PIPELINE
 ***********************************************************************/


export struct PipelineInfo_Compute : public PipelineInfo
{
    MatModel_PassStage shader_stage;
    
    std::vector<PipelineInfoShaderStageInfo> get_stages() const override 
    {
        std::vector<PipelineInfoShaderStageInfo> result;
        {
            PipelineInfoShaderStageInfo stage_info;
            stage_info.stage_name = "compute";
            stage_info.stage = ShaderStage::compute;
            stage_info.resources = shader_stage.resources;
            result.push_back(stage_info);
        }
        return result;
    }
};
REFLECT_STRUCT_DERIVED(PipelineInfo_Compute, PipelineInfo, 
    shader_stage);


/************************************************************************
 * RAYTRACE PIPELINE
 ***********************************************************************/


export struct RTShaderInfo
{
    Name name;
    ShaderStage stage;
    Name shader;
    std::set<Name> resources;
};
REFLECT_STRUCT(RTShaderInfo,
    name, stage, shader, resources);


export enum class RayTracingGroupType
{
    general,
    triangles_hit,
    procedural_hit
};
REFLECT_ENUM(RayTracingGroupType,
    general, triangles_hit, procedural_hit);


export struct RayTracingShaderGroup
{
    Name name;
    
    RayTracingGroupType type;

    std::optional<Name> general;
    std::optional<Name> closest_hit;
    std::optional<Name> any_hit;
    std::optional<Name> intersection;
};
REFLECT_STRUCT(RayTracingShaderGroup,
    name,
    type,
    general,
    closest_hit,
    any_hit,
    intersection);

export struct RayTracingSBTLayoutEntry
{
    Name group;
    Name define;
};
REFLECT_STRUCT(RayTracingSBTLayoutEntry,
    group, define);

export struct RayTracingSBTLayout
{
    std::vector<RayTracingSBTLayoutEntry> raygen;
    std::vector<RayTracingSBTLayoutEntry> miss;
    std::vector<RayTracingSBTLayoutEntry> hit;
    std::vector<RayTracingSBTLayoutEntry> callable;
};
REFLECT_STRUCT(RayTracingSBTLayout,
    raygen, miss, hit, callable);

export struct PipelineInfo_RayTracing : public PipelineInfo
{
    uint32_t max_recursion_depth = 2;
    
    std::vector<RTShaderInfo> shaders;

    std::vector<RayTracingShaderGroup> shader_groups;
    RayTracingSBTLayout sbt;
    
    std::vector<PipelineInfoShaderStageInfo> get_stages() const override 
    {
        std::vector<PipelineInfoShaderStageInfo> result;
        for (auto& info : shaders)
        {
            PipelineInfoShaderStageInfo stage_info;
            stage_info.stage_name = info.name;
            stage_info.stage = info.stage;
            stage_info.resources = info.resources;
            result.push_back(stage_info);
        }
        return result;
    }
};
REFLECT_STRUCT_DERIVED(PipelineInfo_RayTracing, PipelineInfo,
    max_recursion_depth,
    shaders,
    shader_groups,
    sbt
);

/***********************************************************************/

export enum class MatParamType
{
    Float,
    vec2,
    vec3,
    vec4,
    texture,
};
REFLECT_ENUM(MatParamType,
    Float, vec2, vec3, vec4, texture);

export size_t get_mat_param_size(MatParamType type)
{
    switch (type)
    {
        case MatParamType::Float:
            return sizeof(float);
        case MatParamType::vec2:
            return sizeof(glm::vec2);
        case MatParamType::vec3:
            return sizeof(glm::vec3);
        case MatParamType::vec4:
            return sizeof(glm::vec4);
        case MatParamType::texture:
            return sizeof(uint32_t);
    }
    unreachable("Wrong code path");
}

export struct MaterialParamInfo
{
    MatParamType type;
    Name member;
    uint32_t offset;
};
REFLECT_STRUCT(MaterialParamInfo,
    type, member, offset);

export struct MaterialInfo
{
    Name structure;
    std::map<Name, MaterialParamInfo> params;
};
REFLECT_STRUCT(MaterialInfo,
    structure, params);

export using MatModel_PipelineVariant = std::variant<PipelineInfo_Graphics, PipelineInfo_Compute, PipelineInfo_RayTracing>;

export class MaterialModel : public RhObject
{
public:
    Name model_name;
    std::map<Name, std::vector<Name>> enums;
    MatModel_Permutations permutations;
    std::vector<MatModel_PipelineVariant> pipelines;
    
    std::optional<Name> material_resource;
    
    std::optional<MaterialInfo> material_info;
    
    Name set;
    
    const MatModel_PipelineVariant* get_pipeline_config_by_pass(Name pass_name) const;
    
    void on_serialize(const SerializationContext& context) override;
};
REFLECT_OBJECT_FIELDS(MaterialModel, RhObject,
                      model_name, enums, permutations, pipelines, material_info, material_resource);




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