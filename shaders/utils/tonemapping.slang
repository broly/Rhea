#pragma once

float3 ACES(float3 x)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

float3 Uncharted2Tonemap(float3 x)
{
    const float A = 0.15f;
    const float B = 0.50f;
    const float C = 0.10f;
    const float D = 0.20f;
    const float E = 0.02f;
    const float F = 0.30f;

    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 tonemap(float3 color)
{
    float exposure = 2.0f;
    color *= exposure;

    float3 mapped = Uncharted2Tonemap(color);

    const float W = 11.2f;
    float3 whiteScale = 1.0f / Uncharted2Tonemap(float3(W, W, W));
    mapped *= whiteScale;

    return mapped;
}
