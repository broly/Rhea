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

PipelineFamily::PipelineFamily(const GraphicsPipelineDesc& in_desc, std::shared_ptr<RenderBackend> in_backend)
    : desc(in_desc)
    , backend(in_backend)
{
    
}

ShaderKey PipelineFamily::make_shader_key(const std::map<Name, ShaderOptionValue>& in_options) const
{
    std::set<Name> processed_options;
    
    uint64_t bits = 0;
    
    
    uint8_t bit_index = 0;
    
    for (auto& feature : desc.features)
    {
        if (std::holds_alternative<ShaderFeatureEnum>(feature))
        {
            ShaderFeatureEnum feature_enum = std::get<ShaderFeatureEnum>(feature);
            auto option_value = in_options.find(feature_enum.name);
            if (option_value != in_options.end())
            {
                uint16_t num_variants = feature_enum.members.size();
                uint8_t num_bits = log2(num_variants + 1);
                
                checkf(std::holds_alternative<Name>(option_value->second), "Invalid option type");
                auto enum_value = std::get<Name>(option_value->second);
                auto it = std::find(feature_enum.members.begin(), feature_enum.members.end(), enum_value);
                std::size_t index = std::distance(feature_enum.members.begin(), it);
                
                bits |= index << bit_index;
                processed_options.insert(feature_enum.name);
                bit_index += num_bits;
            }
        } else if (std::holds_alternative<ShaderFeatureFlag>(feature))
        {
            auto feature_flag = std::get<ShaderFeatureFlag>(feature);
            auto option_value = in_options.find(feature_flag.name);

            checkf(std::holds_alternative<bool>(option_value->second), "Invalid option type");
            
            bool value = std::get<bool>(option_value->second);
            
            bits |= value << bit_index;
            processed_options.insert(feature_flag.name);
            
            bit_index += 1;
        }
    }
    
    for (auto& [option_name, _] : in_options)
    {
        checkf(processed_options.contains(option_name), "Unidentified option detected: %s", option_name.to_string().c_str());
    }
    return ShaderKey(bits);
}


PipelineObject* PipelineFamily::request_pipeline(ShaderKey key)
{
    uint8_t bit_index = 0;
    std::map<Name, bool> options;
    for (auto& feature : desc.features)
    {
        const uint64_t current_bits = key.key >> bit_index;
        if (std::holds_alternative<ShaderFeatureEnum>(feature))
        {
            auto feature_enum = std::get<ShaderFeatureEnum>(feature);
            uint16_t num_variants = feature_enum.members.size();
            checkf(num_variants != 0, "ShaderFeatureEnum can't contain 0 variants");
            uint8_t num_bits = log2(num_variants + 1);
            
            uint8_t value = current_bits & ((1 << num_bits) - 1);
            for (int32_t index = 0; index < feature_enum.members.size(); index++)
            {
                options[feature_enum.members[index]] = value == index;
            }
            bit_index += num_bits;
        } else if (std::holds_alternative<ShaderFeatureFlag>(feature))
        {
            auto feature_flag = std::get<ShaderFeatureFlag>(feature);
            uint8_t value = current_bits & 1;
            options[feature_flag.name] = value;
            bit_index += 1;
        }
        
        checkf(bit_index < 64, "ShaderKey bits exceeded");
    }
    
    for (auto& stage : desc.stages)
    {
        request_permutation(stage.shader, key, options);
    }
    
    return backend->create_pipeline();
}

std::filesystem::path PipelineFamily::request_permutation(const std::string& shader_name, ShaderKey key, const std::map<Name, bool>& options)
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
            return compiled_permutation_filename;
        }
    }
    
    
    
    std::vector<std::string> new_shader_lines;
    auto shader_lines = file_helpers::load_lines_from_file(shader_path);

    auto version_line = shader_lines[0];
    shader_lines.erase(shader_lines.begin());
    
    new_shader_lines.push_back(version_line);
    
    for (auto option : options)
    {
        auto new_line = std::string("#define ") + option.first.to_string() + " " + (option.second ? "1" : "0");
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
    
    return compiled_permutation_filename;
}

