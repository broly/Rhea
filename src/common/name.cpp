module name;

import <future>;
import <iostream>;
import <ostream>;
import <variant>;
import <vector>;
import <unordered_map>;

#include "type_macros.h"

struct NameGlobals
{
    NON_COPYABLE(NameGlobals);
    
    NameGlobals()
    {
        table.push_back("<None>");
        lookup.emplace("<None>", 0);
    }
    
    std::vector<std::string> table;
    std::unordered_map<std::string, uint32_t> lookup;
    std::mutex mutex;
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
    
    std::lock_guard<std::mutex> lock(globals.mutex);

    auto it = globals.lookup.find(key);
    if (it != globals.lookup.end())
        return it->second;
    
    
    uint32_t id = static_cast<uint32_t>(globals.table.size());
    
    if (id == 61 || id == 96)
    {
        std::cout << str << std::endl;
    }
    
    globals.table.push_back(key);
    globals.lookup.emplace(key, id);
    
    
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

Name::Name(const std::string_view& str)
    : id(get_or_create_name_id(std::string(str)))
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
    return globals.table[id];
}
