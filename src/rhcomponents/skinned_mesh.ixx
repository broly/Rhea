export module rhcomponents:skinned_mesh;

import std.compat;
import reflect;
import ecs;

import rhmath;
import assets;
import render;
import glm;

export
{
    // Skinned mesh: geometry goes through MeshRenderer (mesh = the skeletal mesh's render mesh, set by
    // init_skinned_mesh), vertices are skinned on the GPU before the frame is drawn from `pose`.
    struct SkinnedMesh
    {
        SkeletalMeshHandle skeletal_mesh;

        // shared with the renderer: it reads the matrices / morph weights when `version` changes
        [[=rh::transient]] std::shared_ptr<SkinningPose> pose;
        [[=rh::transient]] std::vector<glm::mat4> local_pose;

        // Local (parent relative) bone transforms, one per skeleton bone
        void set_local_pose(const std::vector<glm::mat4>& in_local_pose);
        void reset_to_bind_pose();

        // Morph target (blend shape) weights, one per morph target of the mesh, applied before skinning
        void set_morph_weights(const std::vector<float>& weights);
        const std::vector<float>& get_morph_weights() const { return pose->morph_weights; }

        const Skeleton& get_skeleton() const { return skeletal_mesh.get().skeleton; }
    };

    // Creates the pose and points the entity's MeshRenderer at the skeletal mesh (adds one if missing).
    // Called for entities spawned from JSON; call it after adding a SkinnedMesh in code.
    void init_skinned_mesh(ecs::Registry& registry, ecs::Entity e);
}
