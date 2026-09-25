module;

#include <vulkan/vulkan.h>

export module vk:debug;

import std.compat;

import name;

namespace vk
{
    class Debug
    {
    public:
        void register_vk_image_name(VkImage image, Name debug_name);
        Name get_vk_image_name(VkImage image);
        
        std::map<VkImage, Name> image_names;
    };
}