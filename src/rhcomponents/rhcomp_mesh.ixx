module;

#include <json/value.h>

export module rhcomponents:rhcomp_mesh;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import rhmath;
import assets;
import framework;
import name;
import render_scene;
import render;
import physics;

#include "object/object_reflection_macro.h"


export struct SceneViewProxy_Mesh : public SceneViewProxy_Transform
{
    MeshHandle mesh;
    AABB bounds;
    std::vector<std::shared_ptr<Material>> materials;
    Name debug_name;
    
    // set for skinned meshes (RhComp_SkeletalMesh)
    std::shared_ptr<SkinningPose> skinning;
};


export class RhComp_StaticMesh : public RhComp_Renderable
{
public:    
    
    void on_init() override;
    void start() override;
    void finish() override;
    void on_serialize(const SerializationContext& context) override;
    
    AABB get_aabb() const override;
    
    virtual void update_scene_proxy();
    void on_property_changed(std::string_view property) override;
    
    SceneViewProxy_Mesh scene_proxy;
    
    [[=rh::serialize]] MeshHandle mesh;

    [[=rh::serialize]] std::vector<std::shared_ptr<Material>> mats;

    // Static triangle mesh collision (level geometry), created on start
    [[=rh::serialize, =rh::edit]] bool collision = false;

    // Builds collision_shape from mesh_data (= mesh.get(), slow for big meshes, cached on disk under
    // cache_key). Thread safe: scene importers call it on loading threads with the mesh resolved on
    // the main thread (AssetManager lookups are not); start() builds it if it's missing.
    void cook_collision(phys::PhysicsScene& physics, const StaticMesh& mesh_data, std::string_view cache_key);

    phys::Shape collision_shape;
    phys::BodyId collision_body;

private:
    void create_collision_body();
    void destroy_collision_body();
};

RH_OBJECT(RhComp_StaticMesh)


