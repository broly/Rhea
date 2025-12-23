#include "json_utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <json/reader.h>

#include "paths.h"

std::optional<Json::Value> json_utils::load_json_asset(std::string asset_rel_path)
{
    std::filesystem::path level_path = paths::get_assets_path() / asset_rel_path;
    
    std::ifstream file(level_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open bootstrap_level.json" << std::endl;
        return std::nullopt;
    }
    
    Json::Value root;
    Json::CharReaderBuilder reader;
    std::string errs;
    
    if (!Json::parseFromStream(reader, file, &root, &errs)) {
        std::cerr << "Failed to parse JSON: " << errs << std::endl;
        return std::nullopt;
    }
    
    return root;
}
