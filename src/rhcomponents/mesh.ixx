export module rhcomponents:mesh;

import std.compat;
import reflect;

import rhmath;
import assets;
import name;
import render_scene;
import render;
import physics;
import glm;

export
{
    // A lower detail version of a MeshRenderer's mesh: drawn instead of it once the camera is `distance`
    // (m, times the largest scale of the entity) away from the bounds. Same primitives (material slots) as the mesh.
    struct MeshLod
    {
        MeshHandle mesh;
        float distance = 0.0f;

        bool operator==(const MeshLod&) const = default;
    };

    // A simplified copy of a MeshRenderer's mesh drawn into the shadow maps instead of it: same geometries and
    // primitives (material slots), the surface moved at most `error` (m, world). The shadow pass takes the
    // coarsest one whose error is below a texel of the cascade (GenericRenderGraph::draw_scene_shadow).
    struct ShadowProxy
    {
        MeshHandle mesh;
        float error = 0.0f;

        bool operator==(const ShadowProxy&) const = default;
    };

    // What the mesh scene view processor gets for one mesh entity
    struct SceneViewProxy_Mesh : public SceneViewProxy_Transform
    {
        MeshHandle mesh;
        AABB bounds;
        std::vector<MeshLod> lods;
        std::vector<ShadowProxy> shadow_proxies;
        std::vector<std::shared_ptr<Material>> materials;

        // set for skinned meshes (SkinnedMesh)
        std::shared_ptr<SkinningPose> skinning;

        glm::vec4 effect{ 0.0f };   // MeshRenderer::effect
        glm::vec4 tint{ 1.0f, 1.0f, 1.0f, 0.0f };      // MeshRenderer::tint, a: MeshRenderer::dissolve
        glm::vec4 dissolve_edge{ 0.0f };                // MeshRenderer::dissolve_edge_color, a: dissolve_edge_width
    };

    // Renders a mesh at the entity's WorldTransform, one material per material slot of the mesh
    struct MeshRenderer
    {
        MeshHandle mesh;
        // by ascending distance
        std::vector<MeshLod> lods;
        std::vector<std::shared_ptr<Material>> materials;
        [[=rh::edit]] bool visible = true;
        // per instance effect, set by game code every frame it changes (hit flash): rgb linear emission added on
        // top of the materials (HDR), a: JPEG mask level (characters). Only the primitive table entries are
        // rewritten when it changes.
        [[=rh::transient]] glm::vec4 effect{ 0.0f };
        // Per instance look, without a material of its own (pbr and character models). Changes rewrite only the
        // primitive table entries, except dissolve going from 0 to more or back: the primitives switch to the
        // shader variant that discards (INSTANCE_DISSOLVE), which only dissolving instances pay for.
        //   tint       multiplies the base color (linear)
        //   dissolve   0..1: the share of the surface gone, in noisy blobs over the mesh (its own space, so the
        //              pattern sticks to it), shadows included; 1: invisible
        //   dissolve_edge_color, dissolve_edge_width: emission (linear, HDR) along the edge of the holes, its width
        //              a share of the noise range (0..1)
        [[=rh::edit]] glm::vec3 tint{ 1.0f };
        [[=rh::edit, =rh::speed<0.01f>]] float dissolve = 0.0f;
        [[=rh::edit]] glm::vec3 dissolve_edge_color{ 6.0f, 1.5f, 0.3f };
        [[=rh::edit, =rh::speed<0.01f>]] float dissolve_edge_width = 0.06f;

        // by ascending error (level geometry: GltfScene::shadow_proxy_errors)
        [[=rh::transient]] std::vector<ShadowProxy> shadow_proxies;
        // built on a loading thread: stored as shadow_proxies (with their errors) in PostLoad
        [[=rh::transient]] std::shared_ptr<std::vector<std::pair<StaticMesh, float>>> pending_shadow_proxies;
    };

    // A texture of one material slot of this instance only (a degraded copy, a texture of a state): the slot gets
    // a copy of its material with the texture, the other instances keep theirs. The primitives are rebuilt with
    // it; a replaced copy goes once nothing draws it (Renderer::collect_unused_materials). Not for every frame.
    void set_instance_texture(MeshRenderer& renderer, uint32_t slot, Name parameter, TextureHandle texture);

    // Simplified copy of `mesh` for the shadow maps (ShadowProxy): every primitive simplified until its surface
    // moved max_error (mesh units), parts smaller than that removed (a primitive may lose all its triangles).
    // Only positions matter: vertices split at uv / normal seams are merged first. Thread safe.
    StaticMesh build_shadow_proxy(const StaticMesh& mesh, float max_error);

    // How a MeshCollider approximates its mesh. Characters, simulated bodies, sweeps and queries all
    // collide with it, and their cost grows with the triangles they touch: as simple as gameplay allows.
    enum class MeshCollision : uint8_t
    {
        none,           // no collision
        box,            // bounding box of the mesh (doors, panels, props)
        convex_hull,    // convex hull of the vertices (lamps, columns, small props); box when flat
        simplified,     // triangles simplified within simplify_error (default)
        triangles,      // all render triangles: slow for characters next to dense meshes
    };

    struct MeshCollisionSettings
    {
        MeshCollision type = MeshCollision::simplified;
        float simplify_error = 0.02f;       // m, `simplified`: how far the surface may move
    };

    // Static collision of the entity's MeshRenderer (level geometry). The body is created by the
    // PostLoad / Late systems; the shape is cooked then (or ahead of time, on a loading thread, via
    // `pending`) and cached on disk.
    struct MeshCollider
    {
        [[=rh::edit, =rh::read_only]] MeshCollision type = MeshCollision::simplified;
        [[=rh::edit, =rh::read_only]] float simplify_error = 0.02f;
        // moved by code (doors, platforms): the body follows the entity's WorldTransform (by the next physics
        // step), pushes dynamic bodies, carries characters
        [[=rh::edit, =rh::read_only]] bool kinematic = false;

        [[=rh::transient]] phys::Shape shape;
        [[=rh::transient]] std::shared_ptr<phys::Shape> pending;
        [[=rh::transient]] phys::BodyId body;
        // kinematic: where the body was last sent
        [[=rh::transient]] phys::BodyTransform body_transform;

        MeshCollisionSettings get_settings() const { return { type, simplify_error }; }
    };

    // Collision shape of the mesh, scaled (the body carries position and rotation), cached on disk
    // under cache_key. Slow for big meshes on a cache miss, thread safe: AssetManager lookups are
    // not, so pass the resolved mesh. Invalid for MeshCollision::none and empty meshes.
    phys::Shape cook_mesh_collision(phys::PhysicsScene& physics, const StaticMesh& mesh, glm::vec3 scale,
                                    const MeshCollisionSettings& settings, std::string_view cache_key);

    // World space bounds of a mesh entity
    AABB get_mesh_bounds(const MeshRenderer& renderer, const Transform& world);
}
