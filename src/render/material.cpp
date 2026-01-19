module render:material;

import :material_instance;

import linear_color;
import assets;
#include "common/assertion_macros.h"

import globals;

static std::optional<std::string> parse_texture_string(const std::string& str)
{
    constexpr const char* prefix = "TEXTURE(";
    constexpr char suffix = ')';

    if (!str.starts_with(prefix) || str.back() != suffix)
        return std::nullopt;

    std::string path = str.substr(
        std::strlen(prefix),
        str.size() - std::strlen(prefix) - 1
    );

    if (path.empty())
        return std::nullopt;

    return path;
}

void serialize_json_value(MaterialParameterType& target, const Json::Value& value, DependencyCollector* dc)
{
    if (value.isNumeric())
    {
        target.data = value.asFloat();
    } else if (value.isString())
    {
        const std::string str = value.asString();
        
        if (auto texture_path = parse_texture_string(str); texture_path.has_value())
        {
            target.data = TextureHandle{};
            auto& texture_handle = std::get<TextureHandle>(target.data);

            std::string path = *texture_path;
            texture_handle.pending_path = path;
    
            auto texture_future = AssetManager::get().load_texture_async(path);
    
            dc->push(std::async(
                std::launch::async,
                [&target, texture_future, &texture_handle]() mutable
                {
                    texture_handle = texture_future.get();
                }));
        } else
        {
            target.data = Name(str);
        }
    } else if (value.isObject())
    {
        target.data = LinearColor();
        auto& color = std::get<LinearColor>(target.data);
        reflect::json::do_serialize_json_value(color, value, true, dc);
    }
}

// void Material::create_resource()
// {
//     RenderResourceDesc desc{
//         .name = "material",
//         .stages = ShaderStage::fragment,
//         .usage_type = ResourceUsageType::persistent,
//         .sampler = surface_sampler,
//         .variables = {
//             { "material",  SET_MATERIAL, BINDING_UBO_MATERIAL, sizeof(MaterialUBO) },
//             { "u_base_color", SET_MATERIAL, BINDING_SAMPLER_ALBEDO },
//             { "u_emissive", SET_MATERIAL, BINDING_SAMPLER_EMISSIVE },
//             { "u_normal_map", SET_MATERIAL, BINDING_SAMPLER_NORMAL },
//             { "u_orm", SET_MATERIAL, BINDING_SAMPLER_ORM },
//         },
//     });
//     
//     resource = RhGlobals::engine->renderer->create_material_resource(desc); // add permutations support
// }

std::shared_ptr<MaterialInstance> Material::create_instance(Renderer* renderer) const
{
    const std::shared_ptr<const Material> as_material = std::static_pointer_cast<const Material>(shared_from_this());
    return std::make_shared<MaterialInstance>(as_material, renderer);
}
