// Character locomotion: concepts of ALS-Refactored (gait, stance, rotation modes, overlays, foot IK) on the
// ECS, a physics capsule and the animation module. Reusable for any humanoid on the UE4 mannequin skeleton;
// other creatures write their own controller on the animation module.
export module locomotion;

export import :types;
export import :settings;
export import :character;
export import :animation;
