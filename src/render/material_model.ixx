export module render:material_model;

import name;
import rhobject;
import <map>;
import <vector>;
import <string>;
import :graphics_pipeline_desc;
#include "common/reflect_macros.h"
#include "object/object_reflection_macro.h"


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



export struct MatModel_Pass
{
    Name name;
    std::string requirements;
    std::vector<MatModel_AttributesLayout> vertex_layouts;
    std::map<ShaderStage, std::string> shaders;
    bool depth_test;
    bool depth_write;
    bool translucent;
    std::vector<Name> resources;
    std::vector<MatModel_PushConstant> push_constants;
    CullMode cull_mode;
    FrontFace front_face;
    bool no_color_attachments = false;
    CompareOp compare_op;
    std::optional<DepthBiasInfo> depth_bias;
    Name set;
};
REFLECT_STRUCT(MatModel_Pass,
    name, requirements, shaders, depth_test, push_constants, depth_write, set, no_color_attachments, resources, translucent, cull_mode, front_face, compare_op, depth_bias, vertex_layouts);

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
    std::optional<Name> ubo;
    std::optional<Name> binding;
    std::optional<Name> storage;
    std::set<Name> passes;
};
REFLECT_STRUCT(MatModel_Parameter,
    type, variable, ubo, binding, storage, passes);


export class MaterialModel : public RhObject
{
public:
    Name model_name;
    std::map<Name, std::vector<Name>> enums;
    std::map<Name, MatModel_Parameter> parameters;
    MatModel_Permutations permutations;
    std::vector<MatModel_Pass> passes;
    
    Name sampler;
    
    Name set;
    
    std::map<Name, TypeId> ubo_types;
    
    const MatModel_Pass* get_pass_info(Name pass_name) const;
    
    ResourceUsageType usage_type;
    
    void on_serialize(DependencyCollector* dc) override;
};

REFLECT_OBJECT_FIELDS(MaterialModel, RhObject,
                      model_name, enums, parameters, sampler, permutations, passes, usage_type);

export class RenderResourceInfo : public RhObject
{
public:
    Name name;
    std::optional<Name> sampler;
    std::vector<ShaderStage> stages;
    ResourceUsageType usage;
    std::vector<MatModel_Parameter> variables;
    Name set;
};
REFLECT_OBJECT_FIELDS(RenderResourceInfo, RhObject,
    name, stages, usage, sampler, variables, set);