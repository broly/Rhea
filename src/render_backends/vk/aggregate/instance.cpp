module vk:instance;

import <set>;
import <vulkan/vulkan_core.h>;

import :helpers;

#include "render_backends/vk/vk_macro.h"
import <cassert>;

import log;
#include "logging/log_macro.h"

DEFINE_LOGGER(LogVkInstance, DisplayFn);

constexpr const char* VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation"
};

void vk::Instance::init(GLFWwindow* in_window)
{
    window = in_window;
    // Application and instance
    VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "Rhea";
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &appInfo;
    ici.enabledLayerCount = 1;
    ici.ppEnabledLayerNames = VALIDATION_LAYERS;
    
    VkValidationFeatureEnableEXT enables[] = {
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT
    };

    VkValidationFeaturesEXT validationFeatures{};
    validationFeatures.sType =
        VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
    validationFeatures.enabledValidationFeatureCount = 1;
    validationFeatures.pEnabledValidationFeatures = enables;

    ici.pNext = &validationFeatures;

    // extensions (by GLFW)
    uint32_t ext_count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&ext_count);
    ici.enabledExtensionCount = ext_count;
    ici.ppEnabledExtensionNames = extensions;

    VK_CHECK(
        vkCreateInstance(&ici, nullptr, &instance)
    );
    
    glfwCreateWindowSurface(instance, window, nullptr, &surface);
    
    // physical and logical devices
    
    match_queue_families();
    
    float priority = 1.0f;

    std::vector<VkDeviceQueueCreateInfo> queue_infos;
    std::set<uint32_t> unique_families = {
        queues.graphics,
        queues.present
    };
    
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_TRUE;
    
    for (uint32_t family : unique_families) {
        VkDeviceQueueCreateInfo qi{
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO
        };
        qi.queueFamilyIndex = family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        queue_infos.push_back(qi);
    }
    
    const char* device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    
    
    VkDeviceCreateInfo dci{
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO
    };
    dci.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
    dci.pQueueCreateInfos = queue_infos.data();
    dci.pEnabledFeatures = &features;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = device_extensions;

    VK_CHECK(
        vkCreateDevice(physical_device, &dci, nullptr, &device)
    );
}

void vk::Instance::match_queue_families()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    
    // Choose second 'complete' device (second is target)
    constexpr uint8_t target_device_index = 1;
    
    for (uint8_t index = 0; auto device : devices) {
        auto q = vk::find_queue_families(device, surface);
        if (q.complete() && target_device_index == index) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            LogVkInstance.Log("Chosen GPU: %s (%i)",
                props.deviceName, props.deviceID);
            
            
            VkBool32 presentSupported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(
                device,
                q.present,
                surface,
                &presentSupported
            );

            LogVkInstance.Log("Present support: %i",
                presentSupported);
            
            physical_device = device;
            queues = q;
            break;
        }
        index++;
    }
    
    
    
    assert(physical_device != VK_NULL_HANDLE);
    
    queues_indices[0] = queues.graphics;
    queues_indices[1] = queues.present;
}
