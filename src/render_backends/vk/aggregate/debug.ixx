export module vk:debug;

import name;
import <map>;
import <vulkan/vulkan.h>;

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