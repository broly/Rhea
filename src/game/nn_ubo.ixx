export module game:nn_ubo;

import glm;

#include "object/object_reflection_macro.h"

struct NNPassIndicesUBO {
    glm::ivec2 uOutSize;
    glm::ivec2 _pad0;
    glm::ivec4 uActIn [16];
    glm::ivec4 uActOut[16];
    glm::ivec4 uActRes[16];
    int32_t uPwIdx, uDwIdx, uBiasIdx, _pad1;
};

REFLECT_STRUCT_RUNTIME(NNPassIndicesUBO,
    uOutSize, uActIn, uActOut, uActRes, uPwIdx, uDwIdx, uBiasIdx);