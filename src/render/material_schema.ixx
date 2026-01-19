export module render:material_schema;

import name;
import rhobject;
import <map>;
import <vector>;
import <string>;
#include "common/reflect_macros.h"
#include "object/object_reflection_macro.h"




export struct MatSchema_Permutations
{
    std::map<Name, std::string> flags;
    std::map<Name, std::map<Name, Name>> enums;
};
REFLECT_STRUCT(MatSchema_Permutations,
    flags, enums);




export struct MatSchema_Pass
{
    Name name;
    std::string requirements;
    std::map<Name, std::string> shaders;
};
REFLECT_STRUCT(MatSchema_Pass,
    name, requirements, shaders);




export struct MatSchema_Parameter
{
    Name type;
    std::optional<Name> shader_parameter;
};
REFLECT_STRUCT(MatSchema_Parameter,
    type, shader_parameter);


export class MaterialSchema : public RhObject
{
public:
    Name name;
    std::map<Name, std::vector<Name>> enums;
    std::map<Name, Name> uniform_buffers;
    std::map<Name, MatSchema_Parameter> parameters;
    std::map<Name, MatSchema_Permutations> permutations;
    std::vector<MatSchema_Pass> passes;
    
    std::map<Name, TypeId> ubo_types;
    
    void on_serialize(DependencyCollector* dc) override;
};

REFLECT_OBJECT_FIELDS(MaterialSchema, RhObject,
                      name, enums, uniform_buffers, parameters, permutations, passes);
