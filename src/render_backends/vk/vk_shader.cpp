module vk:shader;

import <cassert>;
import <fstream>;
import <vector>;

#include "vk_macro.h"

static std::vector<char> read_file(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    assert(file);

    size_t size = file.tellg();
    std::vector<char> buffer(size);

    file.seekg(0);
    file.read(buffer.data(), size);

    return buffer;
}

VkShader::VkShader(VkDevice device, const std::string& path)
    : device_(device)
{
    std::vector<char> code = read_file(path);

    VkShaderModuleCreateInfo ci{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
    };
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VK_CHECK(vkCreateShaderModule(
        device_, &ci, nullptr, &shader_module));
}

VkShader::~VkShader()
{
    if (device_)
    {
        vkDestroyShaderModule(device_, shader_module, nullptr);
    }
}