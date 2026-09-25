module;

#include <json/value.h>

export module rhcomponents:rhcomp_skeletal_mesh;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import rhmath;
import assets;
import framework;
import render;
import glm;
import :rhcomp_mesh;

#include "object/object_reflection_macro.h"


// Skinned mesh component. Geometry goes through the regular mesh path
// (SceneViewProcessor_Mesh); vertices are skinned on GPU before the frame is drawn.
export class RhComp_SkeletalMesh : public RhComp_StaticMesh
{
public:
    void on_serialize(const SerializationContext& context) override;

    void update_scene_proxy() override;

    // Local (parent relative) bone transforms, one per skeleton bone.
    void set_local_pose(const std::vector<glm::mat4>& in_local_pose);
    const std::vector<glm::mat4>& get_local_pose() const { return local_pose; }

    void reset_to_bind_pose();

    // Morph target (blend shape) weights, one per morph target of the mesh, applied before skinning.
    void set_morph_weights(const std::vector<float>& in_weights);
    const std::vector<float>& get_morph_weights() const { return pose->morph_weights; }

    [[=rh::serialize]] SkeletalMeshHandle skeletal_mesh;

private:
    void rebuild_skinning_matrices();

    std::vector<glm::mat4> local_pose;
    std::shared_ptr<SkinningPose> pose;
};

RH_OBJECT(RhComp_SkeletalMesh)
