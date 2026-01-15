export module name;

import <string>;
import fixed_string;
import static_name;
import <future>;

export namespace NameDebug
{
    struct Registry
    {
        std::unordered_map<uint32_t, std::string> map;
        std::mutex mutex;
    };
    
    Registry& get_registry()
    {
        static Registry registry;
        return registry;
    }

    inline void register_name(uint32_t hash, std::string_view str)
    {
        auto& registry = get_registry();
        
        
        std::lock_guard lock(registry.mutex);


        registry.map.try_emplace(hash, str);
    }

    inline const char* resolve(uint32_t hash)
    {
        auto& registry = get_registry();
        
        std::lock_guard lock(registry.mutex);

        auto it = registry.map.find(hash);
        return it != registry.map.end()
            ? it->second.c_str()
            : "<unknown>";
    }
}

export enum { NAME_None = 0 };

export class Name
{
public:
    Name() = default;
    Name(const char* str);
    Name(const std::string& str);
    Name(const std::string_view& str);
    Name(decltype(NAME_None))
        : id(0)
    {}
    explicit Name(StaticName n) 
        : id(n.value) 
    {}
    
    template<size_t N>
    explicit Name(const FixedString<N>& str)
        : Name(str.buffer)
    {}
    
    Name(const Name&) = default;
    Name(Name&&) noexcept = default;

    Name& operator=(const Name&) = default;
    Name& operator=(Name&&) noexcept = default;
    Name& operator=(const char* str);
    Name& operator=(const std::string& str);
    
    auto operator<=>(const Name&) const = default;
    
    bool is_none() const
    {
        return id == NAME_None;
    }
    
    uint32_t get_id() const { return id; }
    const std::string& to_string() const;

private:
    uint32_t id = 0;
};



export template<> 
struct std::hash<Name>
{
    size_t operator()(const Name& n) const noexcept
    {
        return n.get_id();
    }
};