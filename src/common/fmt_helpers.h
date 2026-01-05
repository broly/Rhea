#pragma once

// #if defined(__RESHARPER__) || defined(__GNUC__)
// 	#define HIGHLIGHT_FORMAT_IMPL \
// 		[[gnu::format(printf, 1, 2)]]
#if defined(__RESHARPER__)
    #define HIGHLIGHT_FORMAT_IMPL \
    [[rscpp::format(printf, 2, 3)]]
#else
    #define HIGHLIGHT_FORMAT_IMPL
#endif


/**
 * Adds format highlighting in ReSharper and Rider like a printf function.
 * It highlights %s, %i, %f, etc. specs and shows corresponding varargs
 */
#define HIGHLIGHT_FORMAT HIGHLIGHT_FORMAT_IMPL