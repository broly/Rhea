module;

#include <json/value.h>

module rhcomponents;

import :render_sync;
import :mesh;
import :skinned_mesh;
import :camera;
import :light;
import :reflection_capture;
import :sky;
import :scene_view_proxy.mesh;
import :scene_view_proxy.camera;
import :scene_view_proxy.light;
import :scene_view_proxy.reflection_capture;
import :scene_view_proxy.sky;

import std.compat;
import ecs;
import rhmath;
import rhobject;
import reflect;
import name;
import framework;
import render_scene;
import assets;
import physics;
import profile;
import glm;

#include "profiling/profile.h"
#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

namespace
{
    bool same(const vec3& a, const vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
    bool same(const vec4& a, const vec4& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
    bool same(const quat& a, const quat& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
    bool same(const Transform& a, const Transform& b)
    {
        return same(a.position, b.position) && same(a.rotation, b.rotation) && same(a.scale, b.scale);
    }

    template<typename ProcessorT>
    SceneViewProcId processor_id()
    {
        return (SceneViewProcId)reflect::get_default<ProcessorT>()->index;
    }

    // --- building proxies from components ---

    SceneViewProxy_Mesh make_proxy(const ecs::Registry& registry, ecs::Entity e, const MeshRenderer& renderer, const Transform& world)
    {
        SceneViewProxy_Mesh proxy;
        proxy.transform = world;
        proxy.mesh = renderer.mesh;
        proxy.bounds = get_mesh_bounds(renderer, world);
        proxy.lods = renderer.lods;
        proxy.shadow_proxies = renderer.shadow_proxies;
        proxy.materials = renderer.materials;
        if (const SkinnedMesh* skinned = registry.get<SkinnedMesh>(e))
            proxy.skinning = skinned->pose;
        return proxy;
    }

    bool same_proxy(const SceneViewProxy_Mesh& a, const SceneViewProxy_Mesh& b)
    {
        return same(a.transform, b.transform) && a.mesh == b.mesh && a.lods == b.lods
            && a.shadow_proxies == b.shadow_proxies && a.materials == b.materials
            && a.skinning == b.skinning;
    }

    SceneViewProxy_Camera make_proxy(const ecs::Registry&, ecs::Entity, const Camera& camera, const Transform& world)
    {
        SceneViewProxy_Camera proxy;
        proxy.transform = world;
        proxy.fov = camera.fov;
        proxy.near_plane = camera.near_plane;
        proxy.far_plane = camera.far_plane;
        proxy.active = camera.active;
        return proxy;
    }

    bool same_proxy(const SceneViewProxy_Camera& a, const SceneViewProxy_Camera& b)
    {
        return same(a.transform, b.transform) && a.fov == b.fov && a.near_plane == b.near_plane
            && a.far_plane == b.far_plane && a.active == b.active;
    }

    SceneViewProxy_Light make_proxy(const ecs::Registry&, ecs::Entity, const Light& light, const Transform& world)
    {
        SceneViewProxy_Light proxy;
        proxy.transform = world;
        proxy.light_type = light.type;
        proxy.color = light.color;
        proxy.intensity = light.intensity;
        proxy.falloff = light.falloff;
        proxy.visible_in_reflection_probes = light.visible_in_reflection_probes;
        return proxy;
    }

    bool same_proxy(const SceneViewProxy_Light& a, const SceneViewProxy_Light& b)
    {
        return same(a.transform, b.transform) && a.light_type == b.light_type && same(a.color, b.color)
            && a.intensity == b.intensity && a.falloff == b.falloff
            && a.visible_in_reflection_probes == b.visible_in_reflection_probes;
    }

    SceneViewProxy_ReflectionCapture make_proxy(const ecs::Registry&, ecs::Entity, const ReflectionCapture& capture, const Transform& world)
    {
        SceneViewProxy_ReflectionCapture proxy;
        proxy.transform = world;
        proxy.active = capture.active;
        proxy.box_extent = capture.box_extent;
        proxy.box_offset = capture.box_offset;
        proxy.blend_distance = capture.blend_distance;
        proxy.intensity = capture.intensity;
        proxy.update_interval = capture.update_interval;
        proxy.rebake_on_enter = capture.rebake_on_enter;
        return proxy;
    }

    bool same_proxy(const SceneViewProxy_ReflectionCapture& a, const SceneViewProxy_ReflectionCapture& b)
    {
        return same(a.transform, b.transform) && a.active == b.active
            && same(a.box_extent, b.box_extent) && same(a.box_offset, b.box_offset)
            && a.blend_distance == b.blend_distance && a.intensity == b.intensity
            && a.update_interval == b.update_interval && a.rebake_on_enter == b.rebake_on_enter;
    }

    SceneViewProxy_Sky make_proxy(const ecs::Registry& registry, ecs::Entity e, const SkyAtmosphere& atmosphere, const Transform& world)
    {
        SceneViewProxy_Sky proxy;
        proxy.transform = world;
        proxy.atmosphere = atmosphere;
        if (const VolumetricClouds* clouds = registry.get<VolumetricClouds>(e))
            proxy.clouds = *clouds;
        else
            proxy.clouds.enabled = false;
        if (const VolumetricFog* fog = registry.get<VolumetricFog>(e))
            proxy.fog = *fog;
        else
            proxy.fog.enabled = false;
        return proxy;
    }

    // plain data: floats, vectors, flags
    bool same_proxy(const SceneViewProxy_Sky& a, const SceneViewProxy_Sky& b)
    {
        return std::memcmp(&a.atmosphere, &b.atmosphere, sizeof(SkyAtmosphere)) == 0
            && std::memcmp(&a.clouds, &b.clouds, sizeof(VolumetricClouds)) == 0
            && std::memcmp(&a.fog, &b.fog, sizeof(VolumetricFog)) == 0;
    }

    template<typename Data>
    bool wants_proxy(const Data&) { return true; }
    bool wants_proxy(const MeshRenderer& renderer) { return renderer.visible && renderer.mesh.is_valid(); }

    // Registers / resubmits / unregisters the proxies of one component type (see MeshProxy)
    template<typename Data, typename State>
    void sync_proxies(ecs::Registry& registry, SceneView& scene_view, SceneViewProcId id)
    {
        // gone: the data component was removed or the entity does not want to be drawn
        std::vector<ecs::Entity> stale;
        ecs::Query<const State>(registry).each([&] (ecs::Entity e, const State&) {
            const Data* data = registry.get<Data>(e);
            if (!data || !wants_proxy(*data) || !registry.has<WorldTransform>(e))
                stale.push_back(e);
        });
        for (ecs::Entity e : stale)
            registry.remove<State>(e);  // the hook unregisters it

        // new
        std::vector<ecs::Entity> added;
        ecs::Query<const Data, const WorldTransform>(registry).each([&] (ecs::Entity e, const Data& data, const WorldTransform&) {
            if (wants_proxy(data) && !registry.has<State>(e))
                added.push_back(e);
        });
        for (ecs::Entity e : added)
        {
            auto proxy = make_proxy(registry, e, *registry.get<Data>(e), registry.get<WorldTransform>(e)->value);
            scene_view.register_scene_view_proxy(proxy, id, Name(scene::get_name(registry, e)));  // also submits
            registry.add<State>(e, State{ std::move(proxy) });
        }

        // changed
        ecs::Query<State, const Data, const WorldTransform>(registry).each([&] (ecs::Entity e, State& state, const Data& data, const WorldTransform& world) {
            auto proxy = make_proxy(registry, e, data, world.value);
            if (same_proxy(proxy, state.proxy))
                return;
            proxy.render_id = state.proxy.render_id;
            proxy.debug_name = state.proxy.debug_name;
            state.proxy = std::move(proxy);
            scene_view.submit_raw(id, &state.proxy);
        });
    }

    template<typename ProcessorT, typename State>
    void unregister_proxy(ecs::Registry& registry, State& state)
    {
        if (SceneView* scene_view = registry.find_resource<SceneView>())
            scene_view->unregister_scene_view_proxy(state.proxy, processor_id<ProcessorT>());
    }

    [[=ecs::on_remove]]
    void unregister_mesh_proxy(ecs::Registry& registry, ecs::Entity, MeshProxy& state)
    {
        unregister_proxy<SceneViewProcessor_Mesh>(registry, state);
    }

    [[=ecs::on_remove]]
    void unregister_camera_proxy(ecs::Registry& registry, ecs::Entity, CameraProxy& state)
    {
        unregister_proxy<SceneViewProcessor_Camera>(registry, state);
    }

    [[=ecs::on_remove]]
    void unregister_light_proxy(ecs::Registry& registry, ecs::Entity, LightProxy& state)
    {
        unregister_proxy<SceneViewProcessor_Light>(registry, state);
    }

    [[=ecs::on_remove]]
    void unregister_reflection_capture_proxy(ecs::Registry& registry, ecs::Entity, ReflectionCaptureProxy& state)
    {
        unregister_proxy<SceneViewProcessor_ReflectionCapture>(registry, state);
    }

    [[=ecs::on_remove]]
    void unregister_sky_proxy(ecs::Registry& registry, ecs::Entity, SkyProxy& state)
    {
        unregister_proxy<SceneViewProcessor_Sky>(registry, state);
    }

    [[=ecs::on_remove]]
    void destroy_mesh_collider_body(ecs::Registry& registry, ecs::Entity, MeshCollider& collider)
    {
        phys::PhysicsScene* physics = registry.find_resource<phys::PhysicsScene>();
        if (collider.body.is_valid() && physics)
            physics->destroy_body(collider.body);
    }

    [[=scene::on_spawned<SkinnedMesh>]]
    void init_spawned_skinned_mesh(World& world, ecs::Entity e, const SerializationContext&)
    {
        init_skinned_mesh(world.registry, e);
    }

    // Shadow proxies simplified on loading threads (GltfScene) become meshes, before the first render proxy
    [[=ecs::system<ecs::Phase::PostLoad>, =ecs::ambiguous_with<MeshColliderSync>]]
    void store_shadow_proxies(ecs::Query<MeshRenderer> renderers)
    {
        renderers.each([&] (MeshRenderer& renderer) {
            if (!renderer.pending_shadow_proxies)
                return;
            for (auto& [mesh, error] : *renderer.pending_shadow_proxies)
                renderer.shadow_proxies.push_back({ AssetManager::get().store_mesh(std::move(mesh)), error });
            renderer.pending_shadow_proxies.reset();
        });
    }

    // Static bodies of MeshColliders: shapes cooked on loading threads (pending) or now
    // independent of the render proxies, but both take the Registry
    [[=ecs::system<ecs::Phase::PostLoad>, =ecs::system<ecs::Phase::Late>, =ecs::in_set<MeshColliderSync>,
      =ecs::after<scene::TransformPropagation>, =ecs::ambiguous_with<RenderSync>]]
    void create_mesh_colliders(ecs::Registry& registry, ecs::ResMut<phys::PhysicsScene> physics)
    {
        ecs::Query<MeshCollider, const MeshRenderer>(registry).each([&] (ecs::Entity e, MeshCollider& collider, const MeshRenderer& renderer) {
            if (collider.body.is_valid() || collider.type == MeshCollision::none)
                return;

            const Transform world = scene::get_world_transform(registry, e);
            if (collider.pending)
            {
                collider.shape = std::move(*collider.pending);
                collider.pending.reset();
            }
            else if (!collider.shape)
            {
                if (!renderer.mesh.is_valid())
                    return;
                collider.shape = cook_mesh_collision(*physics, renderer.mesh.get(), world.scale.glm(), collider.get_settings(),
                                                     scene::get_name(registry, e));
            }
            if (!collider.shape)
            {
                // nothing to collide with (empty mesh, or simplified away): don't cook again every frame
                collider.type = MeshCollision::none;
                return;
            }

            collider.body = physics->create_body({
                .shape = collider.shape,
                .position = world.position.glm(),
                .rotation = world.rotation.glm(),
                .motion = phys::Motion::fixed,
                .category = phys::Category::static_world,
                .user_data = e.bits(),
            });
        });
    }
}

namespace
{
    // The clouds drift with the wind: the offsets of their noise accumulate, so the wind may change at any time
    [[=ecs::system<ecs::Phase::Update>, =ecs::in_set<CloudWind>]]
    void advance_clouds(ecs::Query<VolumetricClouds> clouds, ecs::Query<VolumetricFog> fogs, ecs::Res<ecs::FrameTime> time)
    {
        // the wisps of the fog: they also rise slowly, the layer keeps changing
        fogs.each([&] (VolumetricFog& fog) {
            const float heading = glm::radians(fog.wind_direction);
            const glm::vec3 wind = glm::vec3(std::sin(heading), -0.15f, -std::cos(heading)) * fog.wind_speed;
            fog.offset = fog.offset.glm() - wind * float(time->dt);
        });

        clouds.each([&] (VolumetricClouds& layer) {
            const float heading = glm::radians(layer.wind_direction);
            const glm::vec3 wind = glm::vec3(std::sin(heading), 0.0f, -std::cos(heading)) * layer.wind_speed;
            // the noise is sampled at position + offset: moving the clouds downwind moves the offset upwind
            const glm::vec3 step = -wind * float(time->dt);
            layer.offset = layer.offset.glm() + step;
            // the detail also sinks through the shapes: their edges keep changing
            layer.detail_offset = layer.detail_offset.glm() + step * (1.0f + layer.turbulence)
                + glm::vec3(0.0f, layer.turbulence * layer.wind_speed * float(time->dt), 0.0f);
        });
    }

    [[=ecs::system<ecs::Phase::Late>, =ecs::in_set<RenderSync>, =ecs::after<scene::TransformPropagation>]]
    void sync_render_proxies(ecs::Registry& registry, ecs::ResMut<SceneView> scene_view)
    {
        PROFILE("sync_render_proxies");
        sync_proxies<MeshRenderer, MeshProxy>(registry, *scene_view, processor_id<SceneViewProcessor_Mesh>());
        sync_proxies<Camera, CameraProxy>(registry, *scene_view, processor_id<SceneViewProcessor_Camera>());
        sync_proxies<Light, LightProxy>(registry, *scene_view, processor_id<SceneViewProcessor_Light>());
        sync_proxies<ReflectionCapture, ReflectionCaptureProxy>(registry, *scene_view, processor_id<SceneViewProcessor_ReflectionCapture>());
        sync_proxies<SkyAtmosphere, SkyProxy>(registry, *scene_view, processor_id<SceneViewProcessor_Sky>());
        scene_view->world_aabb = compute_world_bounds(registry);
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(MeshRenderer, MeshCollider, SkinnedMesh, Camera, Light, ReflectionCapture, SkyAtmosphere,
        VolumetricClouds, VolumetricFog)
}

AABB compute_world_bounds(ecs::Registry& registry)
{
    AABB result = { glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 0.f, 0.f) };
    ecs::Query<const MeshProxy>(registry).each([&] (const MeshProxy& mesh) {
        result += mesh.proxy.bounds;
    });
    return result;
}

std::optional<AABB> get_entity_bounds(const ecs::Registry& registry, ecs::Entity e)
{
    if (const MeshProxy* mesh = registry.get<MeshProxy>(e))
        return mesh->proxy.bounds;
    return std::nullopt;
}
