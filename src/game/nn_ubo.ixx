export module game:nn_ubo;

import glm;

#include "object/object_reflection_macro.h"

export struct NNPassIndicesUBO
{
    glm::ivec2  uOutSize;
    int32_t     _pad0[2];          // align to vec4

    int32_t     uActIn[64];
    int32_t     uActOut[64];
    int32_t     uActRes[64];

    int32_t     uPwIdx;
    int32_t     uDwIdx;
    int32_t     uBiasIdx;
    int32_t     _pad1;
};

REFLECT_STRUCT_RUNTIME(NNPassIndicesUBO,
    uOutSize, uActIn, uActOut, uActRes, uPwIdx, uDwIdx, uBiasIdx);