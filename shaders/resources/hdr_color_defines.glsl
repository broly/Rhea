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
        
#endif