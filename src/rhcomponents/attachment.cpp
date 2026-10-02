module;

#include <json/value.h>

module rhcomponents;

import :attachment;
import :skinned_mesh;

import std.compat;
import ecs;
import rhmath;
import rhobject;
import reflect;
import framework;
import assets;
import glm;

#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

namespace
{
    // after the animation (Update), before WorldTransform is computed
    [[=ecs::system<ecs::Phase::Late>, =ecs::in_set<BoneAttachments>, =ecs::before<scene::TransformPropagation>]]
    void follow_bones(ecs::Query<Transform, BoneAttachment, const ChildOf> attached, ecs::Query<const SkinnedMesh> skinned)
    {
        attached.each([&] (Transform& transform, BoneAttachment& attachment, const ChildOf& parent) {
            const SkinnedMesh* mesh = skinned.get(parent.parent);
            if (!mesh || !mesh->skeletal_mesh.is_valid() || mesh->local_pose.empty())
                return;
            const Skeleton& skeleton = mesh->get_skeleton();
            if (attachment.resolved_bone != attachment.bone)
            {
                attachment.resolved_bone = attachment.bone;
                const std::optional<uint32_t> bone = skeleton.find_bone(attachment.bone);
                attachment.bone_index = bone ? int32_t(*bone) : -1;
            }
            if (attachment.bone_index < 0 || size_t(attachment.bone_index) >= mesh->local_pose.size())
                return;

            // the bone in the space of the skinned entity (as Skeleton::compute_skinning_matrices)
            glm::mat4 bone(1.0f);
            for (int32_t b = attachment.bone_index; b >= 0; b = skeleton.bones[size_t(b)].parent)
                bone = mesh->local_pose[size_t(b)] * bone;
            bone = skeleton.root_transform * bone;

            const glm::vec3 euler = glm::radians(attachment.rotation.glm());
            const glm::mat4 offset = glm::translate(glm::mat4(1.0f), attachment.position.glm())
                * glm::mat4_cast(glm::quat(euler))
                * glm::scale(glm::mat4(1.0f), glm::vec3(attachment.scale));

            glm::vec3 scale;
            glm::quat rotation;
            glm::vec3 translation;
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(bone * offset, scale, rotation, translation, skew, perspective);
            transform.position = translation;
            transform.rotation = glm::normalize(rotation);
            transform.scale = scale;
        });
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(BoneAttachment)
}
