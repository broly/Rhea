#version 450

// Ground foliage (FoliageRenderer, src/game/foliage_renderer.*): the plants the culling grew (foliage_cull.comp),
// one indexed indirect draw per bucket (type x variant x LOD), gl_InstanceIndex is the instance. The mesh is the
// bucket's; the plant is turned, scaled, tilted with the ground, bent by the wind and pushed aside by characters,
// more the higher a vertex is.

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "resources/camera.glsl"
#include "resources/mesh_table.glsl"
#include "resources/foliage.glsl"
#include "resources/foliage_instances.glsl"
#include "foliage/ground_foliage.glsl"

FOLIAGE_VARYINGS(out)

const float PI = 3.14159265;

// Horizontal lean of a tip by the wind, in heights: a steady part, gusts rolling along the wind, a flutter
vec2 wind_lean(vec2 xz, float time, float phase)
{
    const vec2 direction = u_foliage.globals.wind.xy;
    const float strength = u_foliage.globals.wind.z;
    const float gust_strength = u_foliage.globals.wind.w;
    const float along = dot(xz, direction);
    const float across = dot(xz, vec2(-direction.y, direction.x));
    float gust = foliage_noise(vec2(along - time * u_foliage.globals.gust.x, across * 0.5) * u_foliage.globals.gust.y);
    gust = smoothstep(0.3, 0.85, gust);
    const float sway = sin(time * 1.9 + phase) * 0.6 + sin(time * 3.3 + phase * 1.7) * 0.3;
    const float lean = strength * (0.6 + 0.4 * sway) + gust_strength * gust * (0.8 + 0.2 * sway);
    const float flutter = sin(time * 4.7 + phase * 2.3) * 0.06 * (strength + gust * gust_strength);
    return direction * lean * 0.45 + vec2(-direction.y, direction.x) * flutter;
}

// Characters push the plants aside (in heights)
vec2 push_lean(vec3 root)
{
    vec2 lean = vec2(0.0);
    for (uint i = 0u; i < min(u_foliage.globals.counts.y, FOLIAGE_MAX_INTERACTORS); ++i)
    {
        const vec4 interactor = u_foliage.globals.interactors[i];
        const vec2 offset = root.xz - interactor.xz;
        const float d = length(offset);
        if (d < interactor.w && abs(root.y - interactor.y) < 2.0)
        {
            const float k = 1.0 - d / interactor.w;
            lean += offset / max(d, 1e-3) * k * k * 1.6;
        }
    }
    return lean;
}

// A vertex `height` above the root of a plant `plant_height` tall, leaned by `lean` (in heights): the offset grows
// with the square of the height, the vertex comes down as the plant bends (about its length is kept)
vec3 bend(vec3 p, vec3 root, float plant_height, vec2 lean)
{
    const float h = clamp((p.y - root.y) / max(plant_height, 1e-3), 0.0, 1.5);
    const vec2 offset = lean * plant_height * h * h;
    const float drop = dot(offset, offset) / max(2.0 * plant_height * max(h, 0.05), 1e-4);
    return vec3(p.x + offset.x, p.y - min(drop, (p.y - root.y) * 0.9), p.z + offset.y);
}

void main()
{
    const FoliageInstance instance = u_foliage_instances.instances[gl_InstanceIndex];
    const FoliageBucket bucket = u_foliage_buckets.buckets[instance.info.z];
    const uint type_index = bucket.mesh.z;
    const FoliageType type = u_foliage.types[type_index];
    const vec3 root = instance.position_scale.xyz;
    const float scale = instance.position_scale.w;
    uint state = instance.info.x;

    const Vertex v = fetch_indexed_vertex(bucket.mesh.x, gl_VertexIndex);

    // turned, scaled, tilted toward the ground's normal
    const float yaw = foliage_random(state) * 2.0 * PI;
    const float c = cos(yaw), s = sin(yaw);
    const mat3 turn = mat3(c, 0.0, -s, 0.0, 1.0, 0.0, s, 0.0, c);
    const vec2 normal_xz = unpackSnorm2x16(instance.info.y);
    const vec3 ground = normalize(vec3(normal_xz.x, sqrt(max(1.0 - dot(normal_xz, normal_xz), 0.0)), normal_xz.y));
    const vec3 up = normalize(mix(vec3(0.0, 1.0, 0.0), ground, type.size.w));
    const vec3 axis_x = normalize(cross(up, vec3(0.0, 0.0, 1.0)));
    const vec3 axis_z = cross(axis_x, up);
    const mat3 tilt = mat3(axis_x, up, axis_z);
    const mat3 rotation = tilt * turn;

    const vec3 local = rotation * (v.position * scale);
    vec3 position = root + local;
    const float plant_height = max(bucket.size.x * scale, 0.02);

    const float phase = foliage_random(state) * 2.0 * PI + dot(root.xz, vec2(0.37, 0.61));
    const float time = u_foliage.globals.terrain_info.z;
    const float prev_time = u_foliage.globals.terrain_info.w;
    const vec2 push = push_lean(root);
    const vec2 lean_now = wind_lean(root.xz, time, phase) * type.bounds.z + push;
    const vec2 lean_prev = wind_lean(root.xz, prev_time, phase) * type.bounds.z + push;
    const vec3 prev_position = bend(position, root, plant_height, lean_prev);
    position = bend(position, root, plant_height, lean_now);

    // color of the plant: brightness / hue variation, some dry, large scale patches
    const float shade = foliage_random(state);
    const float hue = foliage_random(state);
    vec3 color = type.tint.rgb * mix(1.0 - type.tint.a, 1.0 + type.tint.a, shade);
    color *= mix(vec3(1.0), vec3(1.08, 1.0, 0.8), type.tint.a * hue);
    if (foliage_random(state) < type.dry.a)
        color *= mix(vec3(1.0), type.dry.rgb * 2.0, mix(0.5, 1.0, foliage_random(state)));
    const float n1 = foliage_noise(root.xz * 0.07 + 3.1);
    const float n2 = foliage_noise(root.xz * 0.21 + 7.7);
    color *= mix(1.0, 0.75 + 0.5 * n1, type.color.x);
    color = mix(color, color * vec3(1.15, 1.05, 0.65), type.color.x * smoothstep(0.55, 0.85, n2));

    const vec3 normal = normalize(rotation * v.normal);
    const vec3 tangent = normalize(rotation * v.tangent.xyz);
    const float camera_distance = distance(root, u_foliage.globals.camera.xyz);
    // height along the plant: ambient occlusion toward the root
    const float along = clamp((v.position.y * scale) / plant_height, 0.0, 1.0);

    v_world_pos = position;
    v_normal = normal;
    v_tangent = tangent;
    v_bitangent = cross(normal, tangent) * v.tangent.w;
    v_uv = v.uv;
    v_color = color;
    v_curr_clip = camera_ubo.proj * camera_ubo.view * vec4(position, 1.0);
    v_prev_clip = camera_ubo.prev_proj * camera_ubo.prev_view * vec4(prev_position, 1.0);
    v_info = uvec4(bucket.mesh.y, type_index, 0u, 0u);
    // x: occlusion toward the root, y: how much the normal turns up (far plants), z: transmission scale
    v_params = vec4(mix(0.45, 1.0, smoothstep(0.0, 0.6, along)),
                    0.6 * smoothstep(type.fade.z, type.fade.y, camera_distance), type.bounds.w, 0.0);
    gl_Position = v_curr_clip;
}
