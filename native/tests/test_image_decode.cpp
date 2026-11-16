#include <aether/aether.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <initializer_list>

static void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

// Minimal 1x1 opaque red PNG (RGBA 255,0,0,255). Verified against an
// independent decoder before being embedded here.
static const unsigned char kRedPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
    0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89,
    0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54,
    0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F,
    0x00, 0x05, 0x00, 0x01, 0xFF, 0x89, 0x99, 0x3D, 0x1D,
    0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44,
    0xAE, 0x42, 0x60, 0x82,
};

int main() {
    int32_t width = 0;
    int32_t height = 0;
    uint8_t* pixels = nullptr;

    // Opaque red PNG decodes to premultiplied BGRA (0, 0, 255, 255).
    require(aether_image_decode(kRedPng, sizeof(kRedPng), &width, &height, &pixels) == AETHER_OK,
        "valid PNG decode failed");
    require(width == 1 && height == 1, "decoded dimensions must be 1x1");
    require(pixels != nullptr, "decode must produce a pixel buffer");
    require(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 255 && pixels[3] == 255,
        "opaque red pixel must be BGRA 0,0,255,255");
    aether_image_pixels_free(pixels);
    pixels = nullptr;

    // Null input is a caller contract violation.
    require(aether_image_decode(nullptr, 4, &width, &height, &pixels) == AETHER_ERROR_NULL_ARG,
        "null bytes must be rejected as NULL_ARG");
    const uint8_t raw[] = {1, 2, 3, 4};
    require(aether_image_decode(raw, 4, nullptr, &height, &pixels) == AETHER_ERROR_NULL_ARG,
        "null out_width must be rejected as NULL_ARG");
    require(aether_image_decode(raw, 0, &width, &height, &pixels) == AETHER_ERROR_NULL_ARG,
        "empty input must be rejected as NULL_ARG");

    // Garbage bytes are a decode failure.
    require(aether_image_decode(raw, 4, &width, &height, &pixels) == AETHER_ERROR_PARSE_FAILED,
        "garbage bytes must be rejected as PARSE_FAILED");
    require(pixels == nullptr, "failed decode must not produce a buffer");

    // Encoded-byte limit must reject before reading the claimed payload.
    width = 7; height = 9; pixels = reinterpret_cast<uint8_t*>(1);
    require(aether_image_decode(raw, 64u*1024u*1024u+1u, &width, &height, &pixels) == AETHER_ERROR_PARSE_FAILED,
        "encoded payload limit");
    require(width == 7 && height == 9 && pixels == reinterpret_cast<uint8_t*>(1), "failure preserves outputs");
    // Header-only adversarial PNGs: admission rejects both dimension and output-byte limits.
    for (const auto dimensions : {std::pair<unsigned,unsigned>{16385,1}, {16384,16384}}) {
        unsigned char png[sizeof(kRedPng)];
        std::memcpy(png,kRedPng,sizeof(png));
        for (unsigned j=0;j<4;++j) {
            png[16+j]=static_cast<unsigned char>(dimensions.first >> (24-8*j));
            png[20+j]=static_cast<unsigned char>(dimensions.second >> (24-8*j));
        }
        require(aether_image_decode(png,sizeof(png),&width,&height,&pixels)==AETHER_ERROR_PARSE_FAILED,
            "oversize image header rejected");
        require(width == 7 && height == 9 && pixels == reinterpret_cast<uint8_t*>(1), "oversize preserves outputs");
    }
    std::printf("OK: image decode test passed\n");
    return 0;
}
