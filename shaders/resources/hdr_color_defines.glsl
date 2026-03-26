#ifndef HDR_COLOR_DEFINES
#define HDR_COLOR_DEFINES

#ifndef NUM_HDR_COLOR_ARRAY_ELEMENTS
    #error "NUM_HDR_COLOR_ARRAY_ELEMENTS is not provided. Please check json configuration"
    #define NUM_HDR_COLOR_ARRAY_ELEMENTS 1
#endif
#ifndef NUM_HISTORY_ARRAY_ELEMENTS
    #error "NUM_HISTORY_ARRAY_ELEMENTS is not provided. Please check json configuration"
    #define NUM_HISTORY_ARRAY_ELEMENTS 1
#endif

const uint NUM_COLOR_OUTPUTS = NUM_HDR_COLOR_ARRAY_ELEMENTS;

const uint COLOR_OUTPUT_HDR_BASE = 0;
const uint COLOR_OUTPUT_HDR_RTXGI = 1;
const uint COLOR_OUTPUT_HDR_SSR = 2;
const uint COLOR_OUTPUT_HDR_INTERMEDIATE = 3;
const uint COLOR_OUTPUT_HDR_RTXGI_REPROJECTED = 4;
const uint COLOR_OUTPUT_HDR_RTXGI_ACCUM = 5;
const uint COLOR_OUTPUT_HDR_RTXGI_MOMENTS = 6;
const uint COLOR_OUTPUT_HDR_RTXGI_FILTERED = 7;
        
#endif