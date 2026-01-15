export module render:material;

import rhobject;
import <variant>;

import assets;
import linear_color;
import <future>;
import :render_resource;
#include "object/object_reflection_macro.h"




export enum class MaterialBlendMode
{
    opaque = 0,
    masked = 1,
    transparent = 2,
};
REFLECT_ENUM(MaterialBlendMode,
    opaque, masked, transparent);


struct MaterialParameterType
{
    std::variant<float, LinearColor, TextureHandle> data;
};

export void serialize_json_value(MaterialParameterType& target, const Json::Value& value, DependencyCollector* dc);

export class Material : public RhObject
{
public:
    MaterialBlendMode blend_mode = MaterialBlendMode::opaque;
    std::map<std::string, MaterialParameterType> parameters;
    
    void create_resource();
    
    std::shared_ptr<class MaterialInstance> create_instance() const;
    
    RenderResource* resource = nullptr;
};
REFLECT_OBJECT_FIELDS(Material, RhObject,
    blend_mode, parameters);