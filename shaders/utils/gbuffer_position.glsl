#ifndef UTILS_GBUFFER_POSITION
#define UTILS_GBUFFER_POSITION

// World position of the surface at a pixel. The g-buffer keeps no positions (16 bytes a pixel): the linear depth
// (distance along the view axis, float) along the pixel's view ray gives it to the same precision.
// Needs resources/camera.glsl and resources/gbuffer.glsl. Pixels without a surface (linear depth 0): the camera.
vec3 gbuffer_world_position(ivec2 pixel)
{
    const vec2 size = vec2(textureSize(u_gbuffer[GBUFFER_SLOT_LINEAR_DEPTH], 0));
    const vec2 ndc = (vec2(pixel) + 0.5) / size * 2.0 - 1.0;
    const float linear_depth = texelFetch(u_gbuffer[GBUFFER_SLOT_LINEAR_DEPTH], pixel, 0).r;

    // the view ray through the pixel, scaled to reach the linear depth
    const vec4 far_point = camera_ubo.inv_proj * vec4(ndc, 1.0, 1.0);
    const vec3 ray = far_point.xyz / far_point.w;
    const vec3 view_position = ray * (linear_depth / -ray.z);
    return (camera_ubo.inv_view * vec4(view_position, 1.0)).xyz;
}

#endif // UTILS_GBUFFER_POSITION
