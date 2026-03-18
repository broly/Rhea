#ifndef UTILS_HSV
#define UTILS_HSV

vec3 cyclicHSV(float t) {
    float h = fract(t);
    float s = 1.0;
    float v = 1.0;

    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(vec3(h) + K.xyz) * 6.0 - K.www);
    return v * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), s);
}

#endif // UTILS_HSV