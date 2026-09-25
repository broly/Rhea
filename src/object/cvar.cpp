module;

#include <json/value.h>
#include <json/reader.h>
#include <json/writer.h>

module cvar;

import std.compat;
import paths;


static std::vector<cvar::Entry*>& get_registry()
{
    static std::vector<cvar::Entry*> registry;
    return registry;
}

static std::filesystem::path resolve_path(const std::filesystem::path& path)
{
    return path.empty() ? paths::get_cache_path() / "cvars.json" : path;
}

void cvar::register_entry(Entry* entry)
{
    get_registry().push_back(entry);
}

void cvar::unregister_entry(Entry* entry)
{
    std::erase(get_registry(), entry);
}

std::vector<cvar::Entry*> cvar::get_entries()
{
    std::vector<Entry*> entries = get_registry();
    std::ranges::sort(entries, {}, &Entry::get_name);
    return entries;
}

cvar::Entry* cvar::find(std::string_view name)
{
    for (Entry* entry : get_registry())
        if (entry->get_name() == name)
            return entry;
    return nullptr;
}

bool cvar::save(const std::filesystem::path& path)
{
    Json::Value root(Json::objectValue);
    for (Entry* entry : get_entries())
    {
        if (!(entry->get_flags() & persistent))
            continue;
        std::string text = entry->to_string();
        if (!text.empty())
            root[entry->get_name()] = text;
    }

    const std::filesystem::path file_path = resolve_path(path);
    std::filesystem::create_directories(file_path.parent_path());
    std::ofstream file(file_path);
    if (!file)
        return false;

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "\t";
    file << Json::writeString(builder, root);
    return true;
}

bool cvar::load(const std::filesystem::path& path)
{
    std::ifstream file(resolve_path(path));
    if (!file)
        return false;

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors) || !root.isObject())
        return false;

    for (const std::string& name : root.getMemberNames())
    {
        Entry* entry = find(name);
        if (entry && (entry->get_flags() & persistent) && root[name].isString())
            entry->from_string(root[name].asString());
    }
    return true;
}
