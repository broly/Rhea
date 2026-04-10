export module vk:debug_object_tracker;

import <map>;
import <string>;
import name;

namespace vk
{
    bool IsHexChar(char c)
    {
        return std::isxdigit(static_cast<unsigned char>(c));
    }
    
    struct VkDebugObjectTracker
    {
        void register_object(uint64_t object_address, Name object_name)
        {
            object_names.insert({object_address, object_name});
        }
        
        template<typename T>
        requires (sizeof(T) == sizeof(uint64_t) && !std::is_same_v<T, uint64_t>)
        void register_object(T object_address, Name object_name)
        {
            register_object((uint64_t)object_address, object_name);
        }
        
        void unregister_object(uint64_t object_address)
        {
            object_names.erase(object_address);
        }
        
        std::string process_message(const char* input) const
        {
            std::string result;
            const char* p = input;

            while (*p)
            {
                if (p[0] == '0' && p[1] == 'x')
                {
                    const char* start = p;
                    p += 2;

                    const char* hexStart = p;

                    while (IsHexChar(*p))
                        ++p;

                    std::string hexStr(start, p);

                    uint64_t value = std::strtoull(hexStr.c_str(), nullptr, 16);

                    auto it = object_names.find(value);
                    if (it != object_names.end())
                    {
                        result += it->second.to_string();
                        result += " (";
                        result += hexStr;
                        result += ")";
                    }
                    else
                    {
                        result += hexStr;
                    }
                }
                else
                {
                    result += *p;
                    ++p;
                }
            }

            return result;
        }
        
        std::map<uint64_t, Name> object_names;
    };
}