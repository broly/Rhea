export module json_utils;

import <filesystem>;
import <fstream>;
import <iostream>;
import <memory>;
import <optional>;
import <string>;
import <json/reader.h>;

import <json/value.h>;
import <json/writer.h>;

import paths;

export namespace json_utils
{
    extern std::optional<Json::Value> load_json_asset(std::string asset_rel_path)
    {
        std::filesystem::path level_path = paths::get_assets_path() / asset_rel_path;
    
        std::ifstream file(level_path);
        if (!file.is_open()) {
            std::cerr << "Failed to open json object " << level_path.string().c_str() << std::endl;
            throw std::runtime_error("Failed to open json object");
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

    // Write a Json::Value to an absolute filesystem path (pretty-printed).
    // Returns false if the file could not be opened.
    extern bool save_json_to_path(const std::filesystem::path& out_path,
                                  const Json::Value& root)
    {
        std::ofstream file(out_path);
        if (!file.is_open()) {
            std::cerr << "Failed to open json for writing "
                      << out_path.string().c_str() << std::endl;
            return false;
        }

        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
        writer->write(root, &file);
        file << "\n";
        return true;
    }

};
