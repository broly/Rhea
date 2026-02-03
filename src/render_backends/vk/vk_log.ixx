export module vk:log;

import log;
#include "logging/log_macro.h"

DEFINE_LOGGER(LogRB, Warning);
DEFINE_LOGGER(LogRBRenderPass, DisplayFn);
DEFINE_LOGGER(LogVkPipeline, Warning);
DEFINE_LOGGER(LogVkDescriptors, DisplayFn);
DEFINE_LOGGER(LogVkSwapchain, DisplayFn);
DEFINE_LOGGER(LogVkSamplerManager, DisplayFn);
DEFINE_LOGGER(LogVkImageManager, DisplayFn);
DEFINE_LOGGER(LogVkBufferManager, Warning);
DEFINE_LOGGER(LogVkFramebufferManager, DisplayFn);
DEFINE_LOGGER(LogVkShader, Warning);

