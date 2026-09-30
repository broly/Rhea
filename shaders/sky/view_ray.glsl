#ifndef SKY_VIEW_RAY
#define SKY_VIEW_RAY

// World direction through a screen uv ([0, 1], v down). Needs resources/camera.glsl.
vec3 sky_view_ray(vec2 uv)
{
    vec4 far_point = camera_ubo.inv_viewproj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    return normalize(far_point.xyz / far_point.w - camera_ubo.camera_pos.xyz);
}

#endif // SKY_VIEW_RAY
