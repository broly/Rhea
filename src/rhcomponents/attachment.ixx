module;

#include <json/value.h>

export module rhcomponents:attachment;

import std.compat;
import reflect;
import rhobject;
import rhmath;

// An entity that follows a bone of its parent's SkinnedMesh (a weapon in a hand, a hat on the head): its local
// Transform is set every frame (Late, before the transform propagation) to the bone's pose times the offset.
// The entity needs ChildOf the skinned entity.
//
//     "BoneAttachment": { "bone": "hand_r", "position": { "x": 0, "y": 0.02, "z": 0 }, "rotation": { "x": 0, "y": 0, "z": 90 } }
export struct BoneAttachment
{
    [[=rh::edit]] std::string bone = "hand_r";
    // offset in the bone's space
    [[=rh::edit, =rh::speed<0.002f>]] vec3 position{ 0.0f, 0.0f, 0.0f };
    // degrees, applied x, y, z (glm::quat of the euler angles)
    [[=rh::edit, =rh::speed<0.5f>]] vec3 rotation{ 0.0f, 0.0f, 0.0f };
    [[=rh::edit, =rh::speed<0.01f>]] float scale = 1.0f;

    [[=rh::transient]] std::string resolved_bone;
    [[=rh::transient]] int32_t bone_index = -1;
};
