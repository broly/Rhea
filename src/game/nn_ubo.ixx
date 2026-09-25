module;

#include <json/value.h>

export module game:nn_ubo;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import glm;

#include "object/object_reflection_macro.h"

struct NNPassIndicesSSBO {
    [[=rh::padding]] glm::ivec2 _pad0;
    [[=rh::padding]] glm::ivec2 _pad1;
    glm::ivec4 uActIn [16];
    glm::ivec4 uActOut[16];
    glm::ivec4 uActRes[16];
    int32_t uPwIdx, uDwIdx, uBiasIdx;
    [[=rh::padding]] int32_t _pad2;
    
    // Head-only: flat-channel indices into input_packed for the AOVs
    // the head pass needs to reconstruct (baseline and albedo). Encoded
    // as a flat channel index because the AOV's RGB triple may STRADDLE
    // two slices of input_packed; the shader decodes
    //   slot_for_ch     = uActRes[ch / 4]
    //   component_in_v4 = ch % 4
    // for each of the three RGB channels and assembles componentwise.
    //
    // -1 in non-head passes (and ignored by their shaders).
    int  uBaselineFlatCh;
    int  uAlbedoFlatCh;
    [[=rh::padding]] int  _pad3;
    [[=rh::padding]] int  _pad4;
};

RH_REGISTER_TYPE(NNPassIndicesSSBO)

struct NNPassPC
{
    glm::ivec2 out_size;
    uint32_t pass_idx;
    int32_t nn_layout_dw_base;
    int32_t nn_layout_bias_base;
};
RH_REGISTER_TYPE(NNPassPC)