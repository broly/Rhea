#pragma once

// Breaks into the debugger right at the call site. __builtin_debugtrap needs no
// declaration, unlike __debugbreak, whose implicit declarations clash across modules.
#ifndef RH_DEBUGBREAK
#if defined(__clang__)
#define RH_DEBUGBREAK() __builtin_debugtrap()
#else
#define RH_DEBUGBREAK() __debugbreak()
#endif
#endif


// we want to immediately abort when there is an error. 
// In normal engines this would give an error message to the user, or perform a dump of state.

#define VK_CHECK(x)                                                 \
	do                                                              \
	{                                                               \
		VkResult err = x;                                           \
		if (err)                                                    \
		{                                                           \
			std::cout <<"Detected Vulkan error at " << __FILE__ << ":" << __LINE__ << ": " << err << std::endl; \
			RH_DEBUGBREAK(); \
		}                                                           \
	} while (0)

