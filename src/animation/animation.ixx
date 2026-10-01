// Skeletal animation: poses with curves, clips, blend spaces, montages, graphs written as code and the
// Animator component that runs them (see animation:graph for the graph DSL, animation:animator for ECS).
// Creature specific controllers (character locomotion, ...) live in their own modules on top of this one.
export module animation;

export import :core;
export import :ops;
export import :blend_space;
export import :montage;
export import :graph;
export import :ik;
export import :animator;
export import :poses;
export import :spring_bones;
