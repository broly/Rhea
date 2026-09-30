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

    // What the mesh scene view processor gets for one mesh entity
    struct SceneViewProxy_Mesh : public SceneViewProxy_Transform
    {
        MeshHandle mesh;
        AABB bounds;
        std::vector<MeshLod> lods;
        std::vector<std::shared_ptr<Material>> materials;

        // set for skinned meshes (SkinnedMesh)
        std::shared_ptr<SkinningPose> skinning;
    };

    // Renders a mesh at the entity's WorldTransform, one material per material slot of the mesh
    struct MeshRenderer
    {
        MeshHandle mesh;
        // by ascending distance
        std::vector<MeshLod> lods;
        std::vector<std::shared_ptr<Material>> materials;
        [[=rh::edit]] bool visible = true;
    };

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

        [[=rh::transient]] phys::Shape shape;
        [[=rh::transient]] std::shared_ptr<phys::Shape> pending;
        [[=rh::transient]] phys::BodyId body;

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
