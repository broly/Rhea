export module json_utils;

import <filesystem>;
import <fstream>;
import <iostream>;
import <optional>;
import <string>;
import <json/reader.h>;

import <json/value.h>;

import paths;

export namespace json_utils
{
    extern std::optional<Json::Value> load_json_asset(std::string asset_rel_path)
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

};
