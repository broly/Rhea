module name;

import <future>;
import <variant>;
import <vector>;
import <unordered_map>;

namespace
{
    std::vector<std::string> GNameTable;
    std::unordered_map<std::string, uint32_t> GNameLookup;
    std::mutex GNameMutex;
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
    
    
    std::lock_guard<std::mutex> lock(GNameMutex);

    auto it = GNameLookup.find(str);
    if (it != GNameLookup.end())
        return it->second;

    uint32_t id = static_cast<uint32_t>(GNameTable.size());
    GNameTable.push_back(str);
    GNameLookup.emplace(str, id);
    
    
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
    return GNameTable[id];
}
