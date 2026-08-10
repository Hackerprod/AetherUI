// stb_image is the only third-party code in the native core. This translation
// unit is the single place that instantiates its implementation.
#define STB_IMAGE_IMPLEMENTATION
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244) // stb_image conversions are range-checked by its own decode paths
#pragma warning(disable : 4100) // unused parameters in stb_image feature macros
#endif
#include "../third_party/stb_image.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <aether/aether.h>

#include <cstdint>
#include <cstdlib>
#include <limits>

extern "C" {

AETHER_API aether_status_t aether_image_decode(const uint8_t* bytes, size_t byte_count,
                                               int32_t* out_width, int32_t* out_height,
                                               uint8_t** out_pixels) {
    if (!bytes || byte_count == 0 || !out_width || !out_height || !out_pixels) {
        return AETHER_ERROR_NULL_ARG;
    }
    if (byte_count > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return AETHER_ERROR_PARSE_FAILED;
    }

    int width = 0;
    int height = 0;
    int channels_in_file = 0;
    uint8_t* pixels = stbi_load_from_memory(
        bytes, static_cast<int>(byte_count), &width, &height, &channels_in_file, STBI_rgb_alpha);
    if (!pixels) {
        return AETHER_ERROR_PARSE_FAILED;
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        return AETHER_ERROR_PARSE_FAILED;
    }

    // stb_image returns top-down RGBA8 rows with non-premultiplied alpha.
    // Convert in place to the engine's canonical format: BGRA8 with premultiplied
    // alpha, width * 4 bytes per row. Rounding with +127/255 avoids a systematic
    // darkening bias on partially transparent pixels.
    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t index = 0; index < pixel_count; ++index) {
        uint8_t* pixel = pixels + index * 4;
        const uint32_t red = pixel[0];
        const uint32_t green = pixel[1];
        const uint32_t blue = pixel[2];
        const uint32_t alpha = pixel[3];
        if (alpha == 255) {
            pixel[0] = static_cast<uint8_t>(blue);
            pixel[1] = static_cast<uint8_t>(green);
            pixel[2] = static_cast<uint8_t>(red);
            pixel[3] = 255;
        } else {
            pixel[0] = static_cast<uint8_t>((blue * alpha + 127) / 255);
            pixel[1] = static_cast<uint8_t>((green * alpha + 127) / 255);
            pixel[2] = static_cast<uint8_t>((red * alpha + 127) / 255);
            pixel[3] = static_cast<uint8_t>(alpha);
        }
    }

    *out_width = width;
    *out_height = height;
    *out_pixels = pixels;
    return AETHER_OK;
}

AETHER_API void aether_image_pixels_free(uint8_t* pixels) {
    if (pixels) {
        stbi_image_free(pixels);
    }
}

} // extern "C"
