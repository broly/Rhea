#ifndef GROUND_FOLIAGE
#define GROUND_FOLIAGE

// Varyings of the ground foliage (ground_foliage.vert -> ground_foliage.frag)
#define FOLIAGE_VARYINGS(direction) \
    layout(location = 0) direction vec3 v_world_pos; \
    layout(location = 1) direction vec3 v_normal; \
    layout(location = 2) direction vec3 v_tangent; \
    layout(location = 3) direction vec3 v_bitangent; \
    layout(location = 4) direction vec2 v_uv; \
    layout(location = 5) direction vec3 v_color; \
    layout(location = 6) direction vec4 v_curr_clip; \
    layout(location = 7) direction vec4 v_prev_clip; \
    layout(location = 8) flat direction uvec4 v_info; \
    layout(location = 9) direction vec4 v_params;

// v_color:  multiplies the base color (tint, variation, dry plants, patches)
// v_info:   x material, y type
// v_params: x ambient occlusion toward the root, y how much the normal turns up (far plants), z transmission scale

#endif // GROUND_FOLIAGE
