#pragma once
#include <optional>
#include <string>
#include <json/value.h>

namespace json_utils
{
    extern std::optional<Json::Value> load_json_asset(std::string asset_rel_path);
};
