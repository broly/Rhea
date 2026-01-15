module name;

import <future>;
import <variant>;
import <vector>;
import <unordered_map>;

#include "type_macros.h"

struct NameGlobals
{
    DEFAULT_NON_COPYABLE(NameGlobals);
    
    std::vector<std::string> Table;
    std::unordered_map<std::string, uint32_t> Lookup;
    std::mutex Mutex;
};

static NameGlobals& get_name_globals()
{
    static NameGlobals name_globals;
    return name_globals;
}

static std::string normalize_name(std::string_view str)
{
    std::string out;
    out.reserve(str.size());

    for (char c : str)
    {
        if (c >= 'A' && c <= 'Z')
            out.push_back(c + ('a' - 'A'));
        else
            out.push_back(c);
    }
    return out;
}


static uint32_t get_or_create_name_id(const std::string& str)
{
    std::string key = normalize_name(str);
    
    auto& globals = get_name_globals();
    
    std::lock_guard<std::mutex> lock(globals.Mutex);

    auto it = globals.Lookup.find(str);
    if (it != globals.Lookup.end())
        return it->second;

    uint32_t id = static_cast<uint32_t>(globals.Table.size());
    globals.Table.push_back(str);
    globals.Lookup.emplace(str, id);
    
    
    NameDebug::register_name(id, key);
    
    return id;
}

Name::Name(const char* str)
    : id(get_or_create_name_id(str))
{
}

Name::Name(const std::string& str)
    : id(get_or_create_name_id(str))
{
}


Name& Name::operator=(const char* str)
{
    id = get_or_create_name_id(str);
    return *this;
}

Name& Name::operator=(const std::string& str)
{
    id = get_or_create_name_id(str);
    return *this;
}

const std::string& Name::to_string() const
{
    auto& globals = get_name_globals();
    return globals.Table[id];
}
