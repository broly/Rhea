module vk:instance;

import <set>;
import <vulkan/vulkan_core.h>;
import array_helpers;
import :helpers;

#include "render_backends/vk/vk_macro.h"
import <cassert>;
import :device_extension_api;

import log;
#include "logging/log_macro.h"

DEFINE_LOGGER(LogVkInstance, Display);

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
        // VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
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
    
    
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR
    };

    VkPhysicalDeviceProperties2 props2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2
    };

    props2.pNext = &rt_props;

    vkGetPhysicalDeviceProperties2(physical_device, &props2);
    
    this->rt_props = rt_props;
    
    
    
    float priority = 1.0f;

    std::vector<VkDeviceQueueCreateInfo> queue_infos;
    std::set<uint32_t> unique_families = {
        queues.graphics,
        queues.present
    };
    
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_TRUE;
    features.shaderInt64 = VK_TRUE;
    
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
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME
    };
    
    
    VkDeviceCreateInfo dci{
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO
    };
    dci.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
    dci.pQueueCreateInfos = queue_infos.data();
    dci.pEnabledFeatures = &features;
    dci.enabledExtensionCount = array_size(device_extensions);
    dci.ppEnabledExtensionNames = device_extensions;
    //
    // VkPhysicalDeviceBufferDeviceAddressFeatures bufferAddress{
    //     VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES
    // };
    // bufferAddress.bufferDeviceAddress = VK_TRUE;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR
    };
    accelFeatures.accelerationStructure = VK_TRUE;
    // accelFeatures.descriptorBindingAccelerationStructureUpdateAfterBind = VK_TRUE;
    // accelFeatures.pNext = &bufferAddress;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR
    };
    rtFeatures.rayTracingPipeline = VK_TRUE;
    rtFeatures.pNext = &accelFeatures;
    
    
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    // features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    // features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    // features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    // features12.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    // features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
    features12.pNext = &rtFeatures;
    
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.shaderDemoteToHelperInvocation = VK_TRUE;
    features13.pNext = &features12;

    dci.pNext = &features13;

    VK_CHECK(
        vkCreateDevice(physical_device, &dci, nullptr, &device)
    );
    
    vk_ext::load_functions(device);
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
