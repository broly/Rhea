module;

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h" // Included in the Global Module Fragment, implementation is private

export module stb_image;

// Export relevant functions and types with a C++ interface if desired
export namespace stb {
    inline unsigned char* load(const char* filename, int* x, int* y, int* channels, int desired_channels) {
        return stbi_load(filename, x, y, channels, desired_channels);
    }

    inline void free(void* retval_from_load) {
        stbi_image_free(retval_from_load);
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
