module;

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h" // Included in the Global Module Fragment, implementation is private

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

export module stb_image;

// Export relevant functions and types with a C++ interface if desired
export namespace stb {
    inline unsigned char* load(const char* filename, int* x, int* y, int* channels, int desired_channels) {
        return stbi_load(filename, x, y, channels, desired_channels);
    }

    inline void free(void* retval_from_load) {
        stbi_image_free(retval_from_load);
    }
    
    int write_png(char const *filename, int w, int h, int comp, const void  *data, int stride_in_bytes)
    {
        return stbi_write_png(filename, w, h, comp, data, (int)stride_in_bytes);
    }
    
    
    
    enum
    {
        STBI_default = 0, // only used for desired_channels

        STBI_grey       = 1,
        STBI_grey_alpha = 2,
        STBI_rgb        = 3,
        STBI_rgb_alpha  = 4
     };
}
