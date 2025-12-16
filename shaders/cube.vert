#version 450

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 mvp;
} ubo;

vec3 positions[8] = vec3[](
    vec3(-1,-1,-1),
    vec3( 1,-1,-1),
    vec3( 1, 1,-1),
    vec3(-1, 1,-1),
    vec3(-1,-1, 1),
    vec3( 1,-1, 1),
    vec3( 1, 1, 1),
    vec3(-1, 1, 1)
);

int indices[36] = int[](
    0,1,2, 2,3,0,
    4,5,6, 6,7,4,
    0,4,7, 7,3,0,
    1,5,6, 6,2,1,
    3,2,6, 6,7,3,
    0,1,5, 5,4,0
);

void main()
{
    vec3 pos = positions[indices[gl_VertexIndex]];
    gl_Position = ubo.mvp * vec4(pos, 1.0);
}
