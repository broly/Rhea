export module game:nn_ubo;

import glm;

#include "object/object_reflection_macro.h"

struct NNPassIndicesSSBO {
    glm::ivec2 _pad0;
    glm::ivec2 _pad1;
    glm::ivec4 uActIn [16];
    glm::ivec4 uActOut[16];
    glm::ivec4 uActRes[16];
    int32_t uPwIdx, uDwIdx, uBiasIdx;
    int32_t _pad2;
};

REFLECT_STRUCT_RUNTIME(NNPassIndicesSSBO,
    uActIn, uActOut, uActRes, uPwIdx, uDwIdx, uBiasIdx);

struct NNPassPC
{
    glm::ivec2 out_size;
    uint32_t pass_idx;
};
REFLECT_STRUCT_RUNTIME(NNPassPC, out_size, pass_idx);