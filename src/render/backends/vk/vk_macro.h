#pragma once
#include <iostream>
#include <vulkan/vulkan_core.h>

//we want to immediately abort when there is an error. In normal engines this would give an error message to the user, or perform a dump of state.

#define VK_CHECK(x)                                                 \
	do                                                              \
	{                                                               \
		VkResult err = x;                                           \
		if (err)                                                    \
		{                                                           \
			std::cout <<"Detected Vulkan error: " << err << std::endl; \
			abort();                                                \
		}                                                           \
	} while (0)