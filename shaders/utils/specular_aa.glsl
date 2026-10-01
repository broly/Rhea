#ifndef UTILS_SPECULAR_AA
#define UTILS_SPECULAR_AA

// Specular anti-aliasing (Kaplanyan, Tokuyoshi 2016/2019): a normal which changes a lot within a pixel (a
// normal map seen from afar, the bumpy shore of a puddle) spreads the highlight over that pixel. With the
// roughness of the texel alone the highlight lands on single pixels that light up and go out as the camera
// moves (shimmering). The variance of the normal over the pixel (screen derivatives) widens the GGX lobe.
// Fragment shaders only, in uniform control flow (derivatives).
//
// N: shading normal (unit), roughness: perceptual (alpha = roughness^2)

const float SPECULAR_AA_VARIANCE = 0.25;   // screen space filter: variance of the pixel footprint
const float SPECULAR_AA_MAX_KERNEL = 0.18; // largest alpha^2 added (keeps glossy surfaces glossy)

float specular_aa_roughness(vec3 N, float roughness)
{
    vec3 dndu = dFdx(N);
    vec3 dndv = dFdy(N);
    float variance = SPECULAR_AA_VARIANCE * (dot(dndu, dndu) + dot(dndv, dndv));
    float kernel = min(2.0 * variance, SPECULAR_AA_MAX_KERNEL);
    float alpha2 = roughness * roughness * roughness * roughness;
    return sqrt(sqrt(clamp(alpha2 + kernel, 0.0, 1.0)));
}

#endif // UTILS_SPECULAR_AA
