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
    // What the mesh scene view processor gets for one mesh entity
    struct SceneViewProxy_Mesh : public SceneViewProxy_Transform
    {
        MeshHandle mesh;
        AABB bounds;
        std::vector<std::shared_ptr<Material>> materials;

        // set for skinned meshes (SkinnedMesh)
        std::shared_ptr<SkinningPose> skinning;
    };

    // Renders a mesh at the entity's WorldTransform, one material per material slot of the mesh
    struct MeshRenderer
    {
        MeshHandle mesh;
        std::vector<std::shared_ptr<Material>> materials;
        [[=rh::edit]] bool visible = true;
    };

    // Static triangle mesh collision of the entity's MeshRenderer (level geometry). The body is created
    // by the PostLoad / Late systems; the shape is cooked then (or ahead of time, on a loading thread, via
    // `pending`) and cached on disk.
    struct MeshCollider
    {
        [[=rh::transient]] phys::Shape shape;
        [[=rh::transient]] std::shared_ptr<phys::Shape> pending;
        [[=rh::transient]] phys::BodyId body;
    };

    // Triangles of the mesh, scaled (the body carries position and rotation). Slow for big meshes,
    // thread safe: AssetManager lookups are not, so pass the resolved mesh.
    phys::Shape cook_mesh_collision(phys::PhysicsScene& physics, const StaticMesh& mesh, glm::vec3 scale, std::string_view cache_key);

    // World space bounds of a mesh entity
    AABB get_mesh_bounds(const MeshRenderer& renderer, const Transform& world);
}
