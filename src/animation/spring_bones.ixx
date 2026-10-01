export module animation:spring_bones;

import std.compat;
import glm;
import assets;
import reflect;
import framework;
import :core;

// Secondary motion of bone chains (hair, skirt, ears): each chain swings with the character's motion,
// springs back towards the animated pose and is pushed out of collision shapes attached to the body.
// Runs in anim::PostProcess on the animated output, in world space (the entity's Transform), so running,
// turning and jumping all move the hair.
//
// Config: a JSON file (SpringBones::config), loaded once the entity's Animator has a rig:
//
//     {
//       "colliders": [
//         { "name": "head", "bone": "head", "offset": [0, 0.08, 0], "radius": 0.1 },             // sphere
//         { "name": "thigh_l", "bone": "thigh_l", "to": "calf_l", "radius": 0.08 },             // capsule
//         { "name": "chest", "bone": "spine_03", "offset": [-0.07, 0, 0], "to_offset": [0.07, 0, 0], "radius": 0.1 }
//       ],
//       "chains": [
//         { "name": "twintails", "roots": ["hair_twintails_01_l", "hair_twintails_01_r"],
//           "stiffness": 60, "damping": 0.5, "drag": 1, "gravity": 1, "radius": 0.03, "follow": 0.4, "max_angle": 70,
//           "colliders": ["head", "chest"] }                                  // omitted: all colliders
//       ],
//       "jiggles": [
//         { "name": "breasts", "bones": ["breast_l", "breast_r"], "stiffness": 250, "damping": 0.2, "max_offset": 0.02 }
//       ]
//     }
//
// Offsets are in the skeleton's model space at the bind pose (as seen in the bind pose, Y up), they stick
// to the bone. A chain starts at a root bone and follows single children down; the root keeps its animated
// position and turns, the last bone gets a virtual tip one bone length further.
// A jiggle is one bone whose position lags behind the animation on a spring (soft tissue, belly, cheeks):
// the bone moves, it does not turn, by at most max_offset.
export namespace anim
{
    struct SpringCollider
    {
        std::string name;
        uint32_t bone = 0;
        uint32_t to_bone = 0;
        glm::vec3 offset{0.0f};       // bone space
        glm::vec3 to_offset{0.0f};    // to_bone space
        float radius = 0.05f;
        bool capsule = false;

        // world space, this frame and the previous one (substeps interpolate between them)
        glm::vec3 a{0.0f}, b{0.0f};
        glm::vec3 prev_a{0.0f}, prev_b{0.0f};
    };

    struct SpringChain
    {
        std::string name;
        bool enabled = true;
        float stiffness = 60.0f;      // pull towards the animated direction, 1/s^2
        float damping = 0.5f;         // damping ratio of the swing relative to the root: 1 critical, < 1 swings
        float drag = 0.5f;            // air resistance, 1/s, in world space: running blows the chain back
        float gravity = 1.0f;         // m/s^2 down, on top of the animated pose (which already hangs)
        float radius = 0.02f;         // thickness for collisions, m
        float follow = 0.3f;          // 0..1 share of the root's movement applied directly (less lag when running)
        float max_angle = 0.0f;       // degrees from the animated direction, 0: no limit
        std::vector<uint32_t> colliders;   // indices into SpringBones::colliders

        std::vector<uint32_t> bones;  // root first
        glm::vec3 tip_offset{0.0f};   // last bone space: the virtual tip

        // particles: bones then the tip; [0] is the root (kinematic)
        std::vector<glm::vec3> position;
        std::vector<glm::vec3> velocity;
        std::vector<float> length;    // segment i: particle i -> i + 1
        glm::vec3 prev_root{0.0f};
    };

    struct SpringJiggle
    {
        std::string name;
        bool enabled = true;
        uint32_t bone = 0;
        float stiffness = 250.0f;     // 1/s^2
        float damping = 0.2f;         // damping ratio: low wobbles longer
        float gravity = 0.0f;         // m/s^2: sag while moving up and down
        float max_offset = 0.02f;     // m from the animated position

        glm::vec3 position{0.0f};     // world space
        glm::vec3 velocity{0.0f};
        glm::vec3 prev_target{0.0f};
    };

    struct SpringBones
    {
        std::string config;                                    // JSON asset path

        [[=rh::edit]] bool enabled = true;
        [[=rh::edit, =rh::range<0.f, 4.f>]] float stiffness_scale = 1.0f;
        [[=rh::edit, =rh::range<0.f, 4.f>]] float gravity_scale = 1.0f;
        [[=rh::edit, =rh::range<0.f, 4.f>]] float damping_scale = 1.0f;
        [[=rh::edit, =rh::range<0.f, 4.f>]] float drag_scale = 1.0f;
        [[=rh::edit, =rh::range<0.f, 4.f>]] float jiggle_scale = 1.0f;      // jiggle max_offset
        [[=rh::edit, =rh::range<1.f, 16.f>]] int32_t max_substeps = 8;
        [[=rh::edit]] bool draw = false;                       // chains and colliders
        [[=rh::edit]] bool reload = false;                     // reads the config again (tuning)

        [[=rh::transient]] std::vector<SpringChain> chains;
        [[=rh::transient]] std::vector<SpringCollider> colliders;
        [[=rh::transient]] std::vector<SpringJiggle> jiggles;
        [[=rh::transient]] bool loaded = false;
        [[=rh::transient]] bool needs_reset = true;            // next update starts from the animated pose

        bool load(const Rig& rig);
    };

    // Simulates the chains on `local` (the animator's output) and writes the result back into it.
    // model_to_world: the entity's world matrix * the skeleton's root_transform.
    void simulate_spring_bones(SpringBones& springs, const Rig& rig, BonePose& local, const glm::mat4& model_to_world, float dt);
}
