// NUM_HDR_COLOR_ARRAY_ELEMENTS and NUM_HISTORY_ARRAY_ELEMENTS
// must be provided as compiler defines (e.g. -D NUM_HDR_COLOR_ARRAY_ELEMENTS=N)
#ifndef NUM_HDR_COLOR_ARRAY_ELEMENTS
    #error "NUM_HDR_COLOR_ARRAY_ELEMENTS is not provided. Please check json configuration"
    #define NUM_HDR_COLOR_ARRAY_ELEMENTS 1
#endif
#ifndef NUM_HISTORY_ARRAY_ELEMENTS
    #error "NUM_HISTORY_ARRAY_ELEMENTS is not provided. Please check json configuration"
    #define NUM_HISTORY_ARRAY_ELEMENTS 1
#endif

namespace ColorOutput
{
    public static const uint NumOutputs = NUM_HDR_COLOR_ARRAY_ELEMENTS;
    
    public enum Slot
    {
        BASE                  = 0,
        RTXGI                 = 1,
        SSR                   = 2,
        INTERMEDIATE          = 3,
        RESERVED_0            = 4,
        RTXGI_REPROJECTED     = 5,
        RTXGI_ACCUM           = 6,
        RTXGI_MOMENTS         = 7,
        RTXGI_FILTERED        = 8,
        RTXGI_NEURAL_DENOISED = 9,
        RESERVED_1            = 10,
    }

}
