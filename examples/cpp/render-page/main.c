/* Example consumer: renders a real login page with the public surface API
   only (ciclo 02, cierre de la Etapa 2). No text measurement or rasterization
   in the consumer: only <aether/aether.h> + libc. The PNG bytes are embedded
   (copied byte for byte from native/tests/background_image_fixtures.h) so the
   example is self-contained; only the two fonts come from argv.
   Usage: aetherui_render_page regular.ttf bold.ttf output-prefix */
#include <aether/aether.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* red1.png: 1x1 opaque red. */
static const unsigned char kRed1Png[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
    0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0xF8, 0xCF, 0xC0, 0xF0,
    0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x56, 0xC7, 0x2F, 0x0D, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};
/* rgbw2.png: 2x2 red/green/blue/white. Registered but unused by this page;
   it shows the table accepts more than one image. */
static const unsigned char kRgbw2Png[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D, 0x24, 0x00, 0x00, 0x00,
    0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0xF8, 0xCF, 0xC0, 0xF0,
    0x1F, 0x0C, 0x81, 0x34, 0x18, 0x00, 0x00, 0x49, 0xC8, 0x09, 0xF7, 0x03,
    0xD9, 0x64, 0xF1, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
    0x42, 0x60, 0x82,
};

static void check(int cond, const char* msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        exit(1);
    }
}

static uint8_t* read_file(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    long n;
    uint8_t* bytes;
    check(f != NULL, "open font");
    check(fseek(f, 0, SEEK_END) == 0, "seek font");
    n = ftell(f);
    check(n > 0, "font size");
    check(fseek(f, 0, SEEK_SET) == 0, "rewind font");
    *size = (size_t)n;
    bytes = (uint8_t*)malloc(*size);
    check(bytes != NULL, "alloc font");
    check(fread(bytes, 1, *size, f) == *size, "read font");
    check(fclose(f) == 0, "close font");
    return bytes;
}

static void check_pixel(const uint8_t* pixels, uint64_t stride, int x, int y,
                        unsigned b, unsigned g, unsigned r, unsigned a) {
    size_t at = (size_t)y * (size_t)stride + (size_t)x * 4;
    const uint8_t* p = pixels + at;
    if (!(p[0] == b && p[1] == g && p[2] == r && p[3] == a)) {
        fprintf(stderr, "pixel %d,%d got %u,%u,%u,%u expected %u,%u,%u,%u\n",
                x, y, p[0], p[1], p[2], p[3], b, g, r, a);
        exit(1);
    }
}

static const char* kPage =
    "<html><body style=\"background-color:#f4f6fa;padding-left:24px;padding-top:24px;width:480px\">"
    "<div style=\"background-color:#ffffff;background-image:url('red1.png');background-repeat:no-repeat;"
    "padding-left:16px;padding-top:12px;padding-bottom:12px\">"
    "<h1 style=\"font-size:24px;font-weight:700;line-height:36px;color:#244466\">Iniciar sesión</h1>"
    "<p>Correo electrónico</p>"
    "<input type=\"text\" value=\"Ana\" style=\"width:160px;padding-left:8px;padding-right:8px;"
    "padding-top:6px;padding-bottom:6px;border-width:1px;border-style:solid;border-color:#223344\">"
    "<button type=\"button\">Entrar</button>"
    "</div></body></html>";

int main(int argc, char** argv) {
    size_t regular_size, bold_size;
    uint8_t* regular;
    uint8_t* bold;
    aether_surface_t surface = NULL;
    aether_html_ltr_options options;
    aether_surface_frame_info info;
    uint8_t* pixels;
    char path[4096];
    FILE* out;

    static const aether_html_ltr_image_v1 images[] = {
        {"red1.png", kRed1Png, sizeof(kRed1Png)},
        {"rgbw2.png", kRgbw2Png, sizeof(kRgbw2Png)},
    };

    check(argc == 4, "usage: aetherui_render_page regular.ttf bold.ttf output-prefix");
    regular = read_file(argv[1], &regular_size);
    bold = read_file(argv[2], &bold_size);
    check(aether_surface_create(640, 320, &surface) == AETHER_OK, "surface create");

    memset(&options, 0, sizeof(options));
    options.struct_size = sizeof(options);
    options.profile = AETHER_HTML_LTR_PROFILE_STATIC_CONTROLS_2;
    options.regular_font = regular;
    options.regular_font_size = regular_size;
    options.bold_font = bold;
    options.bold_font_size = bold_size;
    options.images = images;
    options.image_count = sizeof(images) / sizeof(images[0]);

    check(aether_surface_render_html_ltr(surface, kPage, strlen(kPage), &options) == AETHER_OK,
          "render_html_ltr");
    memset(&info, 0, sizeof(info));
    info.struct_size = sizeof(info);
    check(aether_surface_copy_frame(surface, &info, NULL, 0) == AETHER_OK, "copy frame info");
    check(info.width == 640 && info.height == 320 && info.stride == 2560 &&
              info.byte_count == 819200 && info.sequence == 1 &&
              info.format == AETHER_PIXEL_FORMAT_BGRA8_PREMULTIPLIED,
          "frame_info 640x320 stride 2560 byte_count 819200 sequence 1 BGRA8");
    pixels = (uint8_t*)malloc((size_t)info.byte_count);
    check(pixels != NULL, "alloc frame");
    check(aether_surface_copy_frame(surface, &info, pixels, (size_t)info.byte_count) == AETHER_OK,
          "copy frame pixels");
    /* Points avoid text and controls; geometry fixed by the HTML: body at
       (0,0) with 24px padding; div at (24,24), 480 wide; the 1x1 red tile
       sits at the div origin. */
    check_pixel(pixels, info.stride, 0, 0, 250, 246, 244, 255);
    check_pixel(pixels, info.stride, 639, 319, 250, 246, 244, 255);
    check_pixel(pixels, info.stride, 24, 24, 0, 0, 255, 255);
    check_pixel(pixels, info.stride, 25, 24, 255, 255, 255, 255);
    check_pixel(pixels, info.stride, 500, 30, 255, 255, 255, 255);

    check(snprintf(path, sizeof(path), "%s-login.bgra", argv[3]) > 0, "output path");
    out = fopen(path, "wb");
    check(out != NULL, "open output");
    check(fwrite(pixels, 1, (size_t)info.byte_count, out) == (size_t)info.byte_count,
          "write frame");
    check(fclose(out) == 0, "close output");
    printf("frame_info width=%" PRId32 " height=%" PRId32 " stride=%" PRIu64
           " byte_count=%" PRIu64 " sequence=%" PRIu64 " format=%" PRIu32 "\n",
           info.width, info.height, info.stride, info.byte_count, info.sequence, info.format);
    check(aether_surface_destroy(surface) == AETHER_OK, "surface destroy");
    free(pixels);
    free(regular);
    free(bold);
    return 0;
}
