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



export struct MatModel_Pass
{
    Name name;
    std::string requirements;
    std::map<Name, std::vector<Name>> enum_whitelist;
    std::map<ShaderStage, std::string> shaders;
    bool depth_test;
    bool depth_write;
    bool translucent;
};
REFLECT_STRUCT(MatModel_Pass,
    name, requirements, shaders, depth_test, depth_write, translucent);

export struct MatModel_Parameter
{
    Name type;
    bool dynamic;
    std::optional<Name> shader_parameter;
    std::optional<Name> ubo;
    std::optional<uint16_t> binding;
};
REFLECT_STRUCT(MatModel_Parameter,
    type, dynamic, shader_parameter, ubo, binding);

export struct MatModel_UniformBuffer
{
    Name type_name;
    uint16_t binding;
};
REFLECT_STRUCT(MatModel_UniformBuffer,
    type_name, binding);



export class MaterialModel : public RhObject
{
public:
    Name model_name;
    std::map<Name, std::vector<Name>> enums;
    std::map<Name, MatModel_UniformBuffer> uniform_buffers;
    std::map<Name, MatModel_Parameter> parameters;
    MatModel_Permutations permutations;
    std::vector<MatModel_Pass> passes;
    
    Name sampler;
    
    uint16_t set;
    
    std::map<Name, TypeId> ubo_types;
    
    const MatModel_Pass* get_pass_info(Name pass_name) const;
    
    ResourceUsageType usage_type;
    
    void on_serialize(DependencyCollector* dc) override;
};
REFLECT_OBJECT_FIELDS(MaterialModel, RhObject,
                      model_name, enums, uniform_buffers, set, parameters, sampler, permutations, passes, usage_type);
