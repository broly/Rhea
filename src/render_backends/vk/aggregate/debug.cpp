module vk:debug;

void vk::Debug::register_vk_image_name(VkImage image, Name debug_name)
{
    image_names[image] = debug_name;
}

Name vk::Debug::get_vk_image_name(VkImage image)
{
    return image_names[image];
}
