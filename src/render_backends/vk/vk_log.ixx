export module vk:log;

import log;
#include "logging/log_macro.h"

DEFINE_LOGGER(LogRB, Warning);
DEFINE_LOGGER(LogRBRenderPass, Display);
DEFINE_LOGGER(LogVkPipeline, Display);
DEFINE_LOGGER(LogVkDescriptors, Display);
DEFINE_LOGGER(LogVkSwapchain, Display);
DEFINE_LOGGER(LogVkSamplerManager, Display);
DEFINE_LOGGER(LogVkImageManager, Display);
DEFINE_LOGGER(LogVkBufferManager, Display);
DEFINE_LOGGER(LogVkFramebufferManager, Warning);
DEFINE_LOGGER(LogVkShader, Display);

