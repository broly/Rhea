module;

#include <json/value.h>

module rhcomponents;

import :render_sync;
import :mesh;
import :skinned_mesh;
import :camera;
import :light;
import :reflection_capture;
import :scene_view_proxy.mesh;
import :scene_view_proxy.camera;
import :scene_view_proxy.light;
import :scene_view_proxy.reflection_capture;

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
        proxy.materials = renderer.materials;
        if (const SkinnedMesh* skinned = registry.get<SkinnedMesh>(e))
            proxy.skinning = skinned->pose;
        return proxy;
    }

    bool same_proxy(const SceneViewProxy_Mesh& a, const SceneViewProxy_Mesh& b)
    {
        return same(a.transform, b.transform) && a.mesh == b.mesh && a.materials == b.materials && a.skinning == b.skinning;
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
        return proxy;
    }

    bool same_proxy(const SceneViewProxy_Light& a, const SceneViewProxy_Light& b)
    {
        return same(a.transform, b.transform) && a.light_type == b.light_type && same(a.color, b.color)
            && a.intensity == b.intensity && a.falloff == b.falloff;
    }

    SceneViewProxy_ReflectionCapture make_proxy(const ecs::Registry&, ecs::Entity, const ReflectionCapture& capture, const Transform& world)
    {
        SceneViewProxy_ReflectionCapture proxy;
        proxy.transform = world;
        proxy.active = capture.active;
        proxy.irradiance = capture.irradiance;
        proxy.prefiltered_env = capture.prefiltered_env;
        return proxy;
    }

    bool same_proxy(const SceneViewProxy_ReflectionCapture& a, const SceneViewProxy_ReflectionCapture& b)
    {
        return same(a.transform, b.transform) && a.active == b.active
            && a.irradiance == b.irradiance && a.prefiltered_env == b.prefiltered_env;
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

    template<typename State, typename ProcessorT>
    void unregister_on_remove(ecs::Registry& registry)
    {
        registry.on_remove<State>([] (ecs::Registry& r, ecs::Entity, State& state) {
            if (SceneView* scene_view = r.find_resource<SceneView>())
                scene_view->unregister_scene_view_proxy(state.proxy, processor_id<ProcessorT>());
        });
    }

    void load_reflection_captures(ecs::Registry& registry)
    {
        ecs::Query<ReflectionCapture>(registry).each([&] (ecs::Entity e, ReflectionCapture& capture) {
            if (capture.irradiance.is_valid())
                return;
            const std::string name = scene::get_name(registry, e);
            capture.irradiance = AssetManager::get().load_cubemap("hdr/ibl_" + name + "_irradiance.exr");
            capture.prefiltered_env = AssetManager::get().load_cubemap("hdr/ibl_" + name + "_prefiltered_env.exr");
        });
    }

    // Static bodies of MeshColliders: shapes cooked on loading threads (pending) or now
    void create_mesh_colliders(ecs::Registry& registry, ecs::ResMut<phys::PhysicsScene> physics)
    {
        ecs::Query<MeshCollider, const MeshRenderer>(registry).each([&] (ecs::Entity e, MeshCollider& collider, const MeshRenderer& renderer) {
            if (collider.body.is_valid())
                return;

            const Transform world = scene::get_world_transform(registry, e);
            if (collider.pending)
            {
                collider.shape = std::move(*collider.pending);
                collider.pending.reset();
            }
            if (!collider.shape && renderer.mesh.is_valid())
                collider.shape = cook_mesh_collision(*physics, renderer.mesh.get(), world.scale.glm(), scene::get_name(registry, e));
            if (!collider.shape)
                return;

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
    void sync_render_proxies(ecs::Registry& registry, ecs::ResMut<SceneView> scene_view)
    {
        PROFILE("sync_render_proxies");
        load_reflection_captures(registry);
        sync_proxies<MeshRenderer, MeshProxy>(registry, *scene_view, processor_id<SceneViewProcessor_Mesh>());
        sync_proxies<Camera, CameraProxy>(registry, *scene_view, processor_id<SceneViewProcessor_Camera>());
        sync_proxies<Light, LightProxy>(registry, *scene_view, processor_id<SceneViewProcessor_Light>());
        sync_proxies<ReflectionCapture, ReflectionCaptureProxy>(registry, *scene_view, processor_id<SceneViewProcessor_ReflectionCapture>());
        scene_view->world_aabb = compute_world_bounds(registry);
    }
}

void install_render_components(World& world, SceneView& scene_view)
{
    scene::register_component_type<MeshRenderer>();
    scene::register_component_type<MeshCollider>();
    scene::register_component_type<SkinnedMesh>([] (World& w, ecs::Entity e, const SerializationContext&) {
        init_skinned_mesh(w.registry, e);
    });
    scene::register_component_type<Camera>();
    scene::register_component_type<Light>();
    scene::register_component_type<ReflectionCapture>();

    ecs::Registry& registry = world.registry;
    registry.set_resource_ref(scene_view);

    unregister_on_remove<MeshProxy, SceneViewProcessor_Mesh>(registry);
    unregister_on_remove<CameraProxy, SceneViewProcessor_Camera>(registry);
    unregister_on_remove<LightProxy, SceneViewProcessor_Light>(registry);
    unregister_on_remove<ReflectionCaptureProxy, SceneViewProcessor_ReflectionCapture>(registry);

    registry.on_remove<MeshCollider>([] (ecs::Registry& r, ecs::Entity, MeshCollider& collider) {
        phys::PhysicsScene* physics = r.find_resource<phys::PhysicsScene>();
        if (collider.body.is_valid() && physics)
            physics->destroy_body(collider.body);
    });

    world.schedule.add<&create_mesh_colliders>(ecs::Phase::PostLoad);
    world.schedule.add<&create_mesh_colliders>(ecs::Phase::Late);
    world.schedule.add<&sync_render_proxies>(ecs::Phase::Late);
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
