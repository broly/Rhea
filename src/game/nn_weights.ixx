export module game:nn_weights;

import <filesystem>;
import <fstream>;
import <vector>;
import <map>;
import <string>;
import <cstdint>;
import <cstring>;
import rhobject;
import name;
import paths;
import assets;
import texture_format;
import render;

#include "common/assertion_macros.h"
#include "object/object_reflection_macro.h"

export enum class NNWeightKind
{
    pointwise, depthwise, bias
};
REFLECT_ENUM(NNWeightKind, pointwise, depthwise, bias);

export enum class NNGLTarget
{
    TEXTURE_2D, TEXTURE_2D_ARRAY
};
REFLECT_ENUM(NNGLTarget, TEXTURE_2D, TEXTURE_2D_ARRAY);

export struct NNWeightDesc
{
    NNWeightKind        kind; 
    NNGLTarget        gl_target; 
    uint32_t    width  = 0;
    uint32_t    height = 0;
    uint32_t    depth  = 1;
    std::string file;
    uint64_t    file_bytes = 0;
};

REFLECT_STRUCT(NNWeightDesc,
    kind, gl_target, width, height, depth, file, file_bytes);


export enum class NNDType
{
    fp16, fp32
};
REFLECT_ENUM(NNDType, fp16, fp32);

export class NNWeightsIndex : public RhObject
{
public:
    NNDType dtype {};
    std::map<std::string, NNWeightDesc> weights; 

    // -------------------------------------------------------------------------
    // GPU loading
    // -------------------------------------------------------------------------

    RBImageHandle create_gpu_texture(
        RenderBackend& backend,
        const NNWeightDesc& desc) const
    {
        auto base_dir = paths::get_assets_path();
        
        const TextureFormat format = (dtype == NNDType::fp16)
            ? TextureFormat::RGBA16F
            : TextureFormat::RGBA32F;

        const std::filesystem::path bin_path = base_dir / desc.file;
        std::ifstream file(bin_path, std::ios::binary);
        checkf(file.is_open(), "Could not open %s", bin_path.string().c_str());

        Texture tex_transition;
        tex_transition.name = desc.file;
        tex_transition.transition_path = desc.file;
        tex_transition.format = format;
        tex_transition.extent = Extent{desc.width, desc.height};
        tex_transition.bulk.resize(desc.file_bytes);
        file.read(reinterpret_cast<char*>(tex_transition.bulk.data()), desc.file_bytes);
        
        TextureHandle tex = AssetManager::get().register_external_texture(std::move(tex_transition));

        TextureCreationInfo info{};
        info.format_override = format;
        info.generate_mips = false;
        info.array_layers = (desc.gl_target == NNGLTarget::TEXTURE_2D_ARRAY) ? desc.depth : 1;
        info.current_layout = RBImageLayout::undefined;  

        return backend.create_texture_2d(tex.get(), info);
    }
    
    // IEEE-754 half -> float. Used when source weights are fp16 but the SSBO is
    // declared as float4.
    static float half_to_float(uint16_t h)
    {
        const uint32_t sign = (uint32_t)(h & 0x8000) << 16;
        const uint32_t exp  = (h >> 10) & 0x1F;
        const uint32_t mant = h & 0x3FF;
        uint32_t bits;
        if (exp == 0)
        {
            if (mant == 0)
            {
                bits = sign; // +/- zero
            }
            else
            {
                // subnormal -> normalize
                int e = -1;
                uint32_t m = mant;
                do { m <<= 1; ++e; } while ((m & 0x400) == 0);
                m &= 0x3FF;
                bits = sign | ((uint32_t)(127 - 15 - e) << 23) | (m << 13);
            }
        }
        else if (exp == 0x1F)
        {
            bits = sign | 0x7F800000u | (mant << 13); // inf / nan
        }
        else
        {
            bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
        }
        float out;
        std::memcpy(&out, &bits, sizeof(out));
        return out;
    }

    // Load a weight tensor's raw bytes from disk. The on-disk layout is the texel data the texture path used 
    // (row-major float4 / half4), so reading it  straight into a buffer preserves element ordering. 
    // If the source dtype is fp16 it is expanded to fp32 here, since the SSBO is declared float4.
    std::vector<float> load_weight_ssbo_f32(const NNWeightDesc& desc) const
    {
        auto base_dir = paths::get_assets_path();

        const std::filesystem::path bin_path = base_dir / desc.file;
        std::ifstream file(bin_path, std::ios::binary);
        checkf(file.is_open(), "Could not open %s", bin_path.string().c_str());

        std::vector<std::byte> raw(desc.file_bytes);
        file.read(reinterpret_cast<char*>(raw.data()), desc.file_bytes);

        if (dtype == NNDType::fp32)
        {
            checkf(desc.file_bytes % sizeof(float) == 0,
                "Weight '%s' fp32 byte size not multiple of 4", desc.file.c_str());
            const size_t n = desc.file_bytes / sizeof(float);
            std::vector<float> out(n);
            std::memcpy(out.data(), raw.data(), desc.file_bytes);
            return out;
        }
        else // fp16 -> fp32 expand
        {
            checkf(desc.file_bytes % sizeof(uint16_t) == 0,
                "Weight '%s' fp16 byte size not multiple of 2", desc.file.c_str());
            const size_t n = desc.file_bytes / sizeof(uint16_t);
            std::vector<float> out(n);
            const uint16_t* src = reinterpret_cast<const uint16_t*>(raw.data());
            for (size_t i = 0; i < n; ++i)
                out[i] = half_to_float(src[i]);
            return out;
        }
    }

    // Element count (in float4 units) a weight tensor occupies in its SSBO span.
    static uint32_t slot_float4_count(const NNWeightDesc& desc)
    {
        // Every source texel is one RGBA element == one float4.
        return desc.width * desc.height * desc.depth;
    }

    const NNWeightDesc& find(const std::string& name) const
    {
        auto it = weights.find(name);
        checkf(it != weights.end(), "Weight '%s' not found", name.c_str());
        return it->second;
    }
};

REFLECT_OBJECT_FIELDS(NNWeightsIndex, RhObject,
    dtype,
    weights);
