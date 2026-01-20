module render:pipeline_family;

import <variant>;
import <map>;
import <set>;
import paths;
import <fstream>;
import <filesystem>;
import file_helpers;
import string_helpers;


#include "common/assertion_macros.h"

PipelineFamily::PipelineFamily(Name in_pass_name, std::shared_ptr<MaterialModel> model, std::shared_ptr<RenderBackend> in_backend)
    : backend(in_backend)
    , model(model)
{
    pass = model->get_pass_info(in_pass_name);
    checkf(pass, "MaterialModel has no pass '%s'", in_pass_name.to_string().c_str());
}

ShaderKey PipelineFamily::make_shader_key(const std::map<Name, ShaderOptionValue>& options) const
{
    uint64_t bits = 0;
    uint8_t bit_index = 0;

    std::set<Name> consumed;

    // enum
    for (const auto& [enum_name, enum_values] : model->permutations.enums)
    {
        auto it = options.find(enum_name);
        checkf(it != options.end(), "Missing enum option %s", enum_name.to_string().c_str());
        checkf(std::holds_alternative<Name>(it->second), "Enum option must be Name");

        const Name value = std::get<Name>(it->second);
        auto vit = enum_values.find(value);
        checkf(vit != enum_values.end(), "Invalid enum value");

        const uint32_t index = std::distance(enum_values.begin(), vit);
        const uint8_t bits_needed = uint8_t(std::ceil(std::log2(enum_values.size())));

        bits |= (uint64_t(index) << bit_index);
        bit_index += bits_needed;
        consumed.insert(enum_name);
    }

    // flags
    for (const auto& [flag_name, _] : model->permutations.flags)
    {
        auto it = options.find(flag_name);
        bool value = (it != options.end()) && std::get<bool>(it->second);

        bits |= (uint64_t(value) << bit_index);
        bit_index += 1;
        consumed.insert(flag_name);
    }

    for (const auto& [name, _] : options)
        checkf(consumed.contains(name), "Unknown shader option %s", name.to_string().c_str());

    checkf(bit_index <= 64, "ShaderKey overflow");

    return ShaderKey(bits);
}


PipelineObject* PipelineFamily::request_pipeline(ShaderKey key, const PipelineLayoutDesc& layout)
{
    if (auto it = pipelines.find(key.key); it != pipelines.end())
        return it->second;

    std::map<Name, bool> defines;
    decode_key_to_defines(key, defines);

    GraphicsPipelineDesc desc;
    desc.pass_name = pass->name;

    for (const auto& [stage_enum, shader_name] : pass->shaders)
    {
        GraphicsPipelineStage stage;
        stage.stage = stage_enum;
        stage.compiled_shader = request_permutation(shader_name, key, defines).string();
        desc.stages.push_back(stage);
    }
    desc.layout = layout;

    auto pipeline = backend->create_pipeline(desc);
    pipelines[key.key] = pipeline;

    return pipeline;
}


void PipelineFamily::decode_key_to_defines(ShaderKey key, std::map<Name, bool>& out_defines) const
{
    uint8_t bit_index = 0;

    for (const auto& [enum_name, enum_values] : model->permutations.enums)
    {
        const uint8_t bits_needed =
            uint8_t(std::ceil(std::log2(enum_values.size())));

        uint64_t value = (key.key >> bit_index) & ((1ull << bits_needed) - 1);

        uint32_t idx = 0;
        for (const auto& [name, _] : enum_values)
        {
            out_defines[name] = (idx == value);
            ++idx;
        }

        bit_index += bits_needed;
    }

    for (const auto& [flag_name, _] : model->permutations.flags)
    {
        out_defines[flag_name] = ((key.key >> bit_index) & 1) != 0;
        bit_index += 1;
    }
}

std::filesystem::path PipelineFamily::request_permutation(const std::string& shader_name, ShaderKey key, const std::map<Name, bool>& defines)
{
    const std::filesystem::path shaders_dir = paths::get_project_path() / "shaders";
    const std::filesystem::path shader_path = shaders_dir / shader_name;
    
    const auto [pure_shader_name, shader_extension] = string_helpers::split_by_dot(shader_name);
    
    if (!std::filesystem::exists(shader_path))
    {
        throw std::invalid_argument("shader_path does not exist");
    }

    const std::filesystem::path shaders_cache_dir = paths::get_project_path() / "shaders_cache";
    
    if (!std::filesystem::exists(shaders_cache_dir))
        std::filesystem::create_directory(shaders_cache_dir);

    const std::filesystem::path shader_permutations_dir = shaders_cache_dir / shader_name;
    
    if (!std::filesystem::exists(shader_permutations_dir))
        std::filesystem::create_directory(shader_permutations_dir);
    
    std::string shader_hash = std::to_string(key.key);
    
    const std::string hashed_shader_name = pure_shader_name + "_" + shader_hash + "." + shader_extension;
    
    const std::string compiled_permutation_filename = hashed_shader_name + ".spv";
    
    auto compiled_shader_permutation_file = shader_permutations_dir / compiled_permutation_filename;
    
    
    auto intermediate_dir =  paths::get_project_path() / "intermediate";
    file_helpers::create_dir_if_not_exists(intermediate_dir);
    
    auto permutations_dir = intermediate_dir / "permutations";
    file_helpers::create_dir_if_not_exists(permutations_dir);
    
    auto timestamp_file_name = permutations_dir / (hashed_shader_name + ".timestamp");

    
    if (std::filesystem::exists(compiled_shader_permutation_file))
    {
        
        std::filesystem::file_time_type filetime = std::filesystem::last_write_time(shader_path);
        std::string actual_timestamp = file_helpers::file_time_to_string(filetime);
        std::string saved_timestamp = string_helpers::trim_and_remove_newlines(file_helpers::load_text_from_file(hashed_shader_name + ".timestamp"));
        if (actual_timestamp == saved_timestamp)
        {
            return compiled_shader_permutation_file;
        }
    }
    
    
    
    std::vector<std::string> new_shader_lines;
    auto shader_lines = file_helpers::load_lines_from_file(shader_path);

    auto version_line = shader_lines[0];
    shader_lines.erase(shader_lines.begin());
    
    new_shader_lines.push_back(version_line);
    
    for (auto define : defines)
    {
        auto new_line = std::string("#define ") + define.first.to_string() + " " + (define.second ? "1" : "0");
        new_shader_lines.push_back(new_line);
    }
    
    new_shader_lines.insert(new_shader_lines.end(), shader_lines.begin(), shader_lines.end());
    
    
    auto permutation_source_file = permutations_dir / hashed_shader_name;
    
    file_helpers::save_lines_to_file(permutation_source_file, new_shader_lines);
    
    std::string cmd;
    cmd += "glslc ";
    cmd += permutation_source_file.string() + " ";
    cmd += std::string("-I ") + shaders_dir.string() + " ";
    cmd += "-o " + compiled_shader_permutation_file.string();
    
    std::system("chcp 65001");
    int result = std::system(cmd.c_str());
    if (result == 0)
    {
        std::filesystem::file_time_type filetime = std::filesystem::last_write_time(shader_path);
        std::string timestamp = file_helpers::file_time_to_string(filetime);
        file_helpers::save_text_to_file(timestamp_file_name, timestamp);
    }
    
    return compiled_shader_permutation_file;
}

