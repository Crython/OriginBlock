#ifdef __APPLE__
    // Disable Intel SIMD instructions for Mac to prevent architecture crashes
    #define STBI_NO_SIMD
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>