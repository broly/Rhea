
export module game:nn_pipeline;

import <vector>;
import <map>;
import <string>;
import <cstdint>;
import rhobject;
import name;
import :nn_weights;

#include "common/assertion_macros.h"
#include "object/object_reflection_macro.h"

export struct NNTensorDesc
{
    Name        name;
    uint32_t    channels = 0;
    float       scale = 1.0f;
    uint32_t    slices = 0;
    std::vector<uint32_t> activation_slots; 
};

REFLECT_STRUCT(NNTensorDesc,
    name,
    channels,
    scale,
    slices,
    activation_slots);


export struct NNPassIndicesData 
{
    std::vector<int32_t> uActIn;       // input slices
    std::vector<int32_t> uActOut;      // output slices
    std::vector<int32_t> uActRes;      // residual / second-input / head's baseline+albedo

    int32_t uPwIdx   = -1;             // pointwise weight slot
    int32_t uDwIdx   = -1;             // depthwise weight slot
    int32_t uBiasIdx = -1;             // bias slot
};

REFLECT_STRUCT(NNPassIndicesData,
    uActIn,
    uActOut,
    uActRes,
    uPwIdx,
    uDwIdx,
    uBiasIdx);


export struct NNWeightRef
{
    std::string weight_name;        // key in NNWeightsIndex.weights
    uint32_t    slot = 0;
    NNWeightKind kind; 
};

REFLECT_STRUCT(NNWeightRef,
    weight_name,
    slot,
    kind);


export struct NNPermutationOptions
{
    std::map<Name, Name>     enums;       // ACT -> "NN_ACT_GELU"
    std::map<Name, uint32_t> variants;    // IN_CH -> 32, OUT_CH -> 64, ...
};

REFLECT_STRUCT(NNPermutationOptions,
    enums,
    variants);


export enum class NNPassKind
{
    input, enc, dec, bottleneck, down, up, cat, head
};
REFLECT_ENUM(NNPassKind,
    input, enc, dec, bottleneck, down, up, cat, head);

export struct NNPassDesc
{
    Name name;
    NNPassKind kind; 
    Name pipeline_pass;  // "NN_INPUT", "NN_ENC", ... (MaterialModel.pipelines[*].pass)

    Name output_tensor; // name of tensor where to write (see NNPipeline.tensors)
    std::vector<int32_t> workgroup_size = {8, 8, 1};

    NNPassIndicesData          pass_indices;
    NNPermutationOptions       permutation_options;
    std::vector<NNWeightRef>   weight_refs;
    std::optional<bool> disabled = std::nullopt;
    
    bool is_disabled() const
    {
        return disabled.value_or(false);
    }
};

REFLECT_STRUCT(NNPassDesc, 
    name,
    kind,
    pipeline_pass,
    output_tensor,
    workgroup_size,
    pass_indices,
    permutation_options,
    weight_refs,
    disabled);


// defines arrays sizes (NN_NUM_ACTIVATION_SLOTS, ...).
export struct NNArraySizes 
{
    uint32_t activations = 0;
    uint32_t pw_weights = 0;
    uint32_t dw_weights = 0;
    uint32_t biases = 0;
};

REFLECT_STRUCT(NNArraySizes,
    activations,
    pw_weights,
    dw_weights,
    biases);

export enum class NNAOVKind
{
    noisy, normal, depth, albedo_roughness, mvec, custom
};
REFLECT_ENUM(NNAOVKind,
    noisy, normal, depth, albedo_roughness, mvec, custom);


export enum class NNGLFormat
{
    rgba16f, rg16f, r16f
};

export struct NNIOSpec : RhObject
{
    Name name;
    uint32_t channels = 0;
    NNAOVKind aov_kind; 
    NNGLFormat gl_format;
};

REFLECT_STRUCT(NNIOSpec, 
    name,
    channels,
    aov_kind,
    gl_format);

// root nn pipeline json object
export class NNPipeline : public RhObject
{
public:
    NNGLFormat                 image_format = NNGLFormat::rgba16f;
    NNDType                    weight_dtype;
    NNArraySizes               array_sizes;
    std::vector<NNIOSpec>      io_spec;
    std::vector<NNTensorDesc>  tensors;
    std::vector<NNPassDesc>    passes;
};

REFLECT_OBJECT_FIELDS(NNPipeline, RhObject,
    image_format,
    weight_dtype,
    array_sizes,
    io_spec,
    tensors,
    passes);
