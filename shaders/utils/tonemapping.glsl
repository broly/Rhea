#ifndef TONEMAPPING
#define TONEMAPPING


vec3 ACES(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}


vec3 Uncharted2Tonemap(vec3 x)
{
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;

    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F)) - E/F;
}

vec3 tonemap(vec3 color)
{
    float exposure = 2.0;
    color *= exposure;

    vec3 mapped = Uncharted2Tonemap(color);

    const float W = 11.2;
    vec3 whiteScale = 1.0 / Uncharted2Tonemap(vec3(W));
    mapped *= whiteScale;

    return mapped;
}


#endif // TONEMAPPING