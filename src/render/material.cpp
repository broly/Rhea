module render:material;

import :material_instance;

import linear_color;
import assets;
#include "common/assertion_macros.h"

import globals;

void serialize_json_value(MaterialParameterType& target, const Json::Value& value, DependencyCollector* dc)
{
    if (value.isNumeric())
    {
        target.data = value.asFloat();
    } else if (value.isString())
    {
        target.data = TextureHandle{};
        auto& texture_handle = std::get<TextureHandle>(target.data);
        if (!value.isString())
            return;

        std::string path = value.asString();
        texture_handle.pending_path = path;
    
        auto texture_future = AssetManager::get().load_texture_async(path);
    
        dc->push(std::async(
            std::launch::async,
            [&target, texture_future, &texture_handle]() mutable
            {
                texture_handle = texture_future.get();
            }));
    } else if (value.isObject())
    {
        target.data = LinearColor();
        auto& color = std::get<LinearColor>(target.data);
        reflect::json::do_serialize_json_value(color, value, true, dc);
    }
}

void Material::create_resource()
{
    RenderResourceDesc desc {
        .name = name,
        .stages = ShaderStage::all,
        .variables = {},
    };
    
    resource = RhGlobals::engine->renderer->create_material_resource(desc); // add permutations support
}

std::shared_ptr<MaterialInstance> Material::create_instance() const
{
    checkf(resource != nullptr, "creating material instance is prohibited without render resource");
    const std::shared_ptr<const Material> as_material = std::static_pointer_cast<const Material>(shared_from_this());
    return std::make_shared<MaterialInstance>(as_material);
}
