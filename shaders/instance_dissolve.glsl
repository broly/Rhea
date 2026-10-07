#ifndef INSTANCE_DISSOLVE_GLSL
#define INSTANCE_DISSOLVE_GLSL

// Per instance dissolve (MeshRenderer::dissolve, GPUPrimitiveInfo::tint.a and ::dissolve_edge). Only the shader
// variant INSTANCE_DISSOLVE (pbr and character models) discards: the other instances keep early depth tests.
// The pattern is value noise over the position in the mesh's own space, so it sticks to the mesh (and to the
// skinned body), the same in the g-buffer and the shadow maps.

// noise cells per mesh unit
#define INSTANCE_DISSOLVE_FREQUENCY 5.0

float instance_dissolve_hash(vec3 p)
{
    uvec3 q = uvec3(ivec3(floor(p)) + ivec3(1 << 20));
    uint h = q.x * 0x8da6b343u ^ q.y * 0xd8163841u ^ q.z * 0xcb1ab31fu;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return float(h & 0xffffffu) / 16777216.0;
}

float instance_dissolve_value_noise(vec3 p)
{
    const vec3 i = floor(p);
    const vec3 f = p - i;
    const vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mix(instance_dissolve_hash(i + vec3(0, 0, 0)), instance_dissolve_hash(i + vec3(1, 0, 0)), u.x),
            mix(instance_dissolve_hash(i + vec3(0, 1, 0)), instance_dissolve_hash(i + vec3(1, 1, 0)), u.x), u.y),
        mix(mix(instance_dissolve_hash(i + vec3(0, 0, 1)), instance_dissolve_hash(i + vec3(1, 0, 1)), u.x),
            mix(instance_dissolve_hash(i + vec3(0, 1, 1)), instance_dissolve_hash(i + vec3(1, 1, 1)), u.x), u.y),
        u.z);
}

// 0..1 at a point of the mesh (its own space): two octaves, stretched to cover the range evenly enough that
// dissolve 0..1 eats the surface from nothing to all
float instance_dissolve_noise(vec3 object_position)
{
    const vec3 p = object_position * INSTANCE_DISSOLVE_FREQUENCY;
    const float n = 0.65 * instance_dissolve_value_noise(p) + 0.35 * instance_dissolve_value_noise(p * 2.7 + 17.0);
    return clamp((n - 0.2) / 0.6, 0.0, 0.999);
}

// true: the point is gone (discard it); amount: the dissolved share
bool instance_dissolved(float noise, float amount)
{
    return amount > 0.0 && noise < amount;
}

// emission along the edge of the holes: edge.rgb, fading over edge.a of the noise range past the threshold
vec3 instance_dissolve_edge(float noise, float amount, vec4 edge)
{
    if (amount <= 0.0 || edge.a <= 0.0)
        return vec3(0.0);
    const float t = (noise - amount) / edge.a;
    return t < 1.0 ? edge.rgb * (1.0 - t) * (1.0 - t) : vec3(0.0);
}

#endif // INSTANCE_DISSOLVE_GLSL
