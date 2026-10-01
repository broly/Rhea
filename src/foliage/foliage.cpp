module;

#include <json/value.h>

module foliage;

import std.compat;
import glm;
import rhmath;
import reflect;
import rhobject;
import ecs;
import framework;
import render_scene;
import rhcomponents;
import terrain;
import character_controller;
import assets;
import json_utils;
import paths;
import profile;
import log;

#include "logging/log_macro.h"
#include "profiling/profile.h"
#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

DEFINE_LOGGER(LogFoliage, Log);

namespace
{
    std::shared_ptr<const FoliageAsset> load_foliage_asset(const std::string& path, const SerializationContext& context)
    {
        if (!std::filesystem::exists(paths::get_assets_path() / path))
            return nullptr;
        const std::optional<Json::Value> json = json_utils::load_json_asset(path);
        if (!json)
            return nullptr;
        auto asset = std::make_shared<FoliageAsset>();
        reflect::json::visit_serialize(*json, *asset, context);
        return asset;
    }

    // The foliage of the first terrain with one, and the characters around, for the renderer
    [[=ecs::system<ecs::Phase::Late>, =ecs::after<scene::TransformPropagation>, =ecs::before<RenderSync>,
      =ecs::ambiguous_with<MeshColliderSync>]]
    void sync_foliage(ecs::Query<const TerrainFoliage, const Terrain> foliage,
        ecs::Query<const CharacterMovement, const WorldTransform> characters,
        ecs::Res<ecs::FrameTime> time, ecs::ResMut<SceneView> scene_view)
    {
        PROFILE("sync_foliage");

        SceneViewProcessor_Foliage& processor = scene_view->get_processor<SceneViewProcessor_Foliage>();
        processor.valid = false;
        processor.terrain.reset();

        float interaction_radius = 0.0f;
        foliage.each([&] (const TerrainFoliage& settings, const Terrain& terrain) {
            if (processor.valid || !settings.enabled || !terrain.data || settings.types.empty())
                return;
            processor.valid = true;
            processor.terrain = terrain.data;
            processor.types.clear();
            for (const FoliageType& type : settings.types)
                if (type.loaded && !type.variant_indices.empty())
                    processor.types.push_back(type);
            processor.wind_strength = settings.wind_strength;
            processor.wind_heading = settings.wind_heading;
            processor.gust_speed = settings.gust_speed;
            processor.gust_scale = std::max(settings.gust_scale, 1.0f);
            processor.gust_strength = settings.gust_strength;
            interaction_radius = settings.interaction_radius;
        });
        processor.time = time->time;

        processor.interactors.clear();
        if (!processor.valid || interaction_radius <= 0.0f)
            return;
        characters.each([&] (const CharacterMovement&, const WorldTransform& world) {
            processor.interactors.push_back({ .position = world.value.position.glm(), .radius = interaction_radius });
        });
    }

    // Loads the assets of the types and picks their variants
    [[=scene::on_spawned<TerrainFoliage>]]
    void load_foliage(World& world, ecs::Entity e, const SerializationContext& context)
    {
        ecs::Registry& registry = world.registry;
        TerrainFoliage& settings = *registry.get<TerrainFoliage>(e);
        const std::string entity_name = scene::get_name(registry, e);
        if (!registry.has<Terrain>(e))
            LogFoliage.Log<Error>("TerrainFoliage of '%s' needs a Terrain on the same entity", entity_name.c_str());

        std::map<std::string, std::shared_ptr<const FoliageAsset>> assets;
        uint32_t variant_count = 0;
        for (FoliageType& type : settings.types)
        {
            auto found = assets.find(type.asset);
            if (found == assets.end())
                found = assets.emplace(type.asset, load_foliage_asset(type.asset, context)).first;
            type.loaded = found->second;
            type.variant_indices.clear();
            if (!type.loaded || !type.loaded->material)
            {
                LogFoliage.Log<Error>("Foliage type '%s': asset '%s' not found", type.name.c_str(), type.asset.c_str());
                type.loaded.reset();
                continue;
            }
            const std::vector<FoliageVariant>& variants = type.loaded->variants;
            for (uint32_t index = 0; index < variants.size(); ++index)
            {
                const FoliageVariant& variant = variants[index];
                const bool wanted = type.variants.empty() || std::ranges::contains(type.variants, variant.name);
                if (wanted && variant.lods.size() == 3 && std::ranges::all_of(variant.lods, &MeshHandle::is_valid))
                    type.variant_indices.push_back(index);
            }
            for (const std::string& name : type.variants)
                if (!std::ranges::contains(variants, name, &FoliageVariant::name))
                    LogFoliage.Log<Warning>("Foliage type '%s': no variant '%s' in '%s'", type.name.c_str(), name.c_str(),
                        type.asset.c_str());
            if (type.fade_end <= type.fade_start || type.scale_max < type.scale_min)
                LogFoliage.Log<Warning>("Foliage type '%s': fade_end must be beyond fade_start, scale_max above scale_min",
                    type.name.c_str());
            variant_count += uint32_t(type.variant_indices.size());
        }
        LogFoliage.Log("Ground foliage of '%s': %zu types, %u variants of %zu assets", entity_name.c_str(),
            settings.types.size(), variant_count, assets.size());
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(TerrainFoliage)
}
