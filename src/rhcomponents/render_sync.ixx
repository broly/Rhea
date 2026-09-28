export module rhcomponents:render_sync;

import std.compat;
import ecs;
import rhmath;
import framework;
import render_scene;

import :mesh;
import :camera;
import :light;
import :reflection_capture;

export
{
    // What the renderer currently has for an entity: registered while the entity has the data component
    // (and a WorldTransform), resubmitted when the built proxy differs, unregistered on removal / destroy.
    struct MeshProxy { SceneViewProxy_Mesh proxy; };
    struct CameraProxy { SceneViewProxy_Camera proxy; };
    struct LightProxy { SceneViewProxy_Light proxy; };
    struct ReflectionCaptureProxy { SceneViewProxy_ReflectionCapture proxy; };

    // System sets of render_sync.cpp (component types, hooks and systems register themselves there;
    // the systems need the SceneView resource, ResMut<SceneView>):
    //   Late:      sync_render_proxies (after scene::TransformPropagation), mesh colliders created at runtime
    //   PostLoad:  mesh colliders of a loaded level
    struct RenderSync {};
    struct MeshColliderSync {};

    // Union of the world bounds of all registered meshes (zero box when there are none)
    AABB compute_world_bounds(ecs::Registry& registry);

    // World bounds of the entity's registered mesh, nullopt if it has none
    std::optional<AABB> get_entity_bounds(const ecs::Registry& registry, ecs::Entity e);
}
