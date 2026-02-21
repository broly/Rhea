#pragma once
import <iostream>;
import <vulkan/vulkan_core.h>;


// we want to immediately abort when there is an error. 
// In normal engines this would give an error message to the user, or perform a dump of state.

#define VK_CHECK(x)                                                 \
	do                                                              \
	{                                                               \
		VkResult err = x;                                           \
		if (err)                                                    \
		{                                                           \
			std::cout <<"Detected Vulkan error at " << __FILE__ << ":" << __LINE__ << ": " << err << std::endl; \
			abort();                                                \
		}                                                           \
	} while (0)

