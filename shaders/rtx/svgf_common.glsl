#ifndef SVGF_COMMON
#define SVGF_COMMON


float luminance(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}

struct SVGFPixelData
{
    vec3 color;
    float weight;
    float luminance;

    vec3 normal;
    float depth;

    float sigma;
    float valid;
};

#endif // SVGF_COMMON