export module game:nn_weights;

import <filesystem>;
import <fstream>;
import <vector>;
import <map>;
import <string>;
import <cstdint>;
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
    
    std::vector<std::byte> create_gpu_texture_ssbo(const NNWeightDesc& desc) const
    {
        auto base_dir = paths::get_assets_path();
        
        const std::filesystem::path bin_path = base_dir / desc.file;
        std::ifstream file(bin_path, std::ios::binary);
        checkf(file.is_open(), "Could not open %s", bin_path.string().c_str());
        
        std::vector<std::byte> bulk;
        bulk.reserve(desc.file_bytes);
        
        file.read(reinterpret_cast<char*>(bulk.data()), desc.file_bytes);
        
        return bulk;
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
