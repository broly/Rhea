export module name;

import <string>;
import fixed_string;
import static_name;
import <future>;

export namespace NameDebug
{
    inline std::unordered_map<uint32_t, std::string> Registry;
    inline std::mutex Mutex;

    inline void register_name(uint32_t hash, std::string_view str)
    {
        std::lock_guard lock(Mutex);


        Registry.try_emplace(hash, str);
    }

    inline const char* resolve(uint32_t hash)
    {
        std::lock_guard lock(Mutex);

        auto it = Registry.find(hash);
        return it != Registry.end()
            ? it->second.c_str()
            : "<unknown>";
    }
}

export class Name
{
public:
    Name() = default;
    explicit Name(const char* str);
    Name(const std::string& str);
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
    
    uint32_t get_id() const { return id; }
    const std::string& to_string() const;

private:
    uint32_t id = 0;
};

