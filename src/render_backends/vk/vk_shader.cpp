module vk:shader;

import <cassert>;
import <fstream>;
import <vector>;

#include "vk_macro.h"

static std::vector<uint32_t> read_file(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    assert(file);

    const size_t byte_size = file.tellg();
    assert(byte_size % 4 == 0 && "SPIR-V size must be multiple of 4 bytes");

    std::vector<uint32_t> code(byte_size / 4);

    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), byte_size);

    return code;
}

VkShader::VkShader(VkDevice device, const std::string& path)
    : device_(device)
{
    code = read_file(path);

    VkShaderModuleCreateInfo ci{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
    };
    ci.codeSize = code.size() * sizeof(uint32_t),
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

const std::vector<uint32_t>& VkShader::get_spirv() const
{
    return code;
}
