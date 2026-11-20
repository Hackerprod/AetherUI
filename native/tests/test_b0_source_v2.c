/* ABI v2: aether_b0_source_create_v2 con imágenes (ciclo 01, §3.5).
 * Estilo require() + exit(1) (patrón test_image_decode.cpp): sin assert(),
 * porque en Release (-DNDEBUG) los assert() con efectos se vacían y -Werror
 * convierte las variables en desuso en error (eso rompe hoy test_b0_source.c).
 * Uso: aether_b0_source_v2_test REGULAR_TTF BOLD_TTF */
#include <aether/aether_b0_source.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "background_image_fixtures.h"

static int failures = 0;
static void require(int ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static unsigned char* read_file(const char* path, size_t* n) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long k = ftell(f);
    if (k < 0) {
        fclose(f);
        return NULL;
    }
    *n = (size_t)k;
    rewind(f);
    unsigned char* p = (unsigned char*)malloc(*n + 1);
    if (!p) {
        fclose(f);
        return NULL;
    }
    if (fread(p, 1, *n, f) != *n) {
        free(p);
        fclose(f);
        return NULL;
    }
    p[*n] = 0;
    if (fclose(f) != 0) {
        free(p);
        return NULL;
    }
    return p;
}

static aether_b0_source_diagnostic_v1 diag(void) {
    aether_b0_source_diagnostic_v1 d = {0};
    d.struct_size = sizeof(d);
    d.version = 1;
    return d;
}

/* Misma página que B1 (test_b0_background_image.cpp): div 100x40 por padding,
 * background-image url('red1.png') no-repeat. */
static const char kHtml[] =
    "<!doctype html><html lang=\"es\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>bg image</title><style>"
    "html{font-family:AetherB0;font-size:16px;font-weight:400;line-height:24px;"
    "color:#202020;white-space:nowrap}"
    "body{display:block}"
    ".box{display:block;width:100px;padding-top:20px;padding-bottom:20px;"
    "background-image:url('red1.png');background-repeat:no-repeat}"
    "</style></head><body><div class=\"box\"></div></body></html>";

static aether_b0_source_options_v2 base_options(const unsigned char* regular, size_t rn,
                                                const unsigned char* bold, size_t bn) {
    aether_b0_source_options_v2 o = {0};
    o.struct_size = sizeof(o);
    o.version = 2;
    o.regular_font = regular;
    o.regular_font_size = rn;
    o.bold_font = bold;
    o.bold_font_size = bn;
    return o;
}

static const unsigned char kGarbage[] = "not a png";

int main(int argc, char** argv) {
    require(argc == 3, "uso: aether_b0_source_v2_test REGULAR_TTF BOLD_TTF");
    if (failures) return 1;
    size_t rn = 0, bn = 0;
    unsigned char* regular = read_file(argv[1], &rn);
    unsigned char* bold = read_file(argv[2], &bn);
    require(regular != NULL && bold != NULL, "leer fuentes");
    if (failures) return 1;

    require(aether_b0_source_options_v2_size() == sizeof(aether_b0_source_options_v2),
            "options_v2_size");

    /* A: create_v2 con 1 imagen (red1) -> ok y píxeles esperados. */
    {
        aether_b0_source_image_v1 images[1];
        images[0].source = "red1.png";
        images[0].bytes = kRed1Png;
        images[0].byte_count = sizeof(kRed1Png);
        aether_b0_source_options_v2 o = base_options(regular, rn, bold, bn);
        o.images = images;
        o.image_count = 1;
        aether_b0_source_diagnostic_v1 d = diag();
        aether_b0_source_t s = NULL;
        int rc = aether_b0_source_create_v2(200, 100, kHtml, strlen(kHtml), &o, &s, &d);
        require(rc == AETHER_OK && s != NULL, "A create_v2 red1 ok");
        require(d.status == 0 && d.stage == 0 && d.reason == 0, "A diag limpio");
        if (s) {
            d = diag();
            rc = aether_b0_source_render(s, &d);
            require(rc == AETHER_OK, "A render ok");
            aether_b0_source_frame_info_v1 info = {0};
            info.struct_size = sizeof(info);
            info.version = 1;
            rc = aether_b0_source_copy_frame(s, &info, NULL, 0);
            require(rc == AETHER_OK && info.width == 200 && info.height == 100 &&
                        info.stride == 800 && info.byte_count == 80000 && info.sequence == 1,
                    "A frame 200x100");
            unsigned char* px = (unsigned char*)malloc((size_t)info.byte_count);
            require(px != NULL, "A malloc frame");
            if (px) {
                info.struct_size = sizeof(info);
                info.version = 1;
                rc = aether_b0_source_copy_frame(s, &info, px, (size_t)info.byte_count);
                require(rc == AETHER_OK, "A copy_frame ok");
                const unsigned char* p00 = px;
                const unsigned char* p50 = px + (size_t)(20 * 200 + 50) * 4;
                require(p00[0] == 0 && p00[1] == 0 && p00[2] == 255 && p00[3] == 255,
                        "A pixel (0,0) rojo");
                require(p50[0] == 0 && p50[1] == 0 && p50[2] == 0 && p50[3] == 0,
                        "A pixel (50,20) transparente");
                free(px);
            }
            require(aether_b0_source_destroy(s) == AETHER_OK, "A destroy ok");
        }
    }

    /* B: reserved[] no cero -> INVALID_ARGUMENT. */
    {
        aether_b0_source_options_v2 o = base_options(regular, rn, bold, bn);
        o.reserved[0] = 1;
        aether_b0_source_diagnostic_v1 d = diag();
        aether_b0_source_t s = NULL;
        int rc = aether_b0_source_create_v2(200, 100, kHtml, strlen(kHtml), &o, &s, &d);
        require(rc == AETHER_ERROR_INVALID_ARGUMENT && s == NULL, "B reserved no cero");
    }

    /* C: imagen que excede presupuesto (IHDR 5000x5000) -> etapa INPUT, LIMIT_EXCEEDED. */
    {
        aether_b0_source_image_v1 images[1];
        images[0].source = "huge.png";
        images[0].bytes = kHugeHeaderPng;
        images[0].byte_count = sizeof(kHugeHeaderPng);
        aether_b0_source_options_v2 o = base_options(regular, rn, bold, bn);
        o.images = images;
        o.image_count = 1;
        aether_b0_source_diagnostic_v1 d = diag();
        aether_b0_source_t s = NULL;
        int rc = aether_b0_source_create_v2(200, 100, kHtml, strlen(kHtml), &o, &s, &d);
        require(rc == AETHER_ERROR_LIMIT_EXCEEDED, "C status LIMIT_EXCEEDED");
        require(d.status == AETHER_ERROR_LIMIT_EXCEEDED && d.stage == AETHER_B0_STAGE_INPUT &&
                    d.reason == AETHER_B0_REASON_LIMIT,
                "C diag INPUT/LIMIT");
    }

    /* D: imagen que no decodifica -> etapa INPUT, UNSUPPORTED. */
    {
        aether_b0_source_image_v1 images[1];
        images[0].source = "bad.png";
        images[0].bytes = kGarbage;
        images[0].byte_count = sizeof(kGarbage);
        aether_b0_source_options_v2 o = base_options(regular, rn, bold, bn);
        o.images = images;
        o.image_count = 1;
        aether_b0_source_diagnostic_v1 d = diag();
        aether_b0_source_t s = NULL;
        int rc = aether_b0_source_create_v2(200, 100, kHtml, strlen(kHtml), &o, &s, &d);
        require(rc == AETHER_ERROR_UNSUPPORTED, "D status UNSUPPORTED");
        require(d.status == AETHER_ERROR_UNSUPPORTED && d.stage == AETHER_B0_STAGE_INPUT &&
                    d.reason == AETHER_B0_REASON_UNSUPPORTED,
                "D diag INPUT/UNSUPPORTED");
    }

    /* E: version != 2 y struct_size erróneo -> INVALID_ARGUMENT. */
    {
        aether_b0_source_options_v2 o = base_options(regular, rn, bold, bn);
        o.version = 1;
        aether_b0_source_diagnostic_v1 d = diag();
        aether_b0_source_t s = NULL;
        int rc = aether_b0_source_create_v2(200, 100, kHtml, strlen(kHtml), &o, &s, &d);
        require(rc == AETHER_ERROR_INVALID_ARGUMENT, "E version 1 rechazada");
        o = base_options(regular, rn, bold, bn);
        o.struct_size = 0;
        d = diag();
        rc = aether_b0_source_create_v2(200, 100, kHtml, strlen(kHtml), &o, &s, &d);
        require(rc == AETHER_ERROR_INVALID_ARGUMENT, "E struct_size 0 rechazado");
    }

    /* F: image_count > 0 sin tabla -> INVALID_ARGUMENT. */
    {
        aether_b0_source_options_v2 o = base_options(regular, rn, bold, bn);
        o.images = NULL;
        o.image_count = 1;
        aether_b0_source_diagnostic_v1 d = diag();
        aether_b0_source_t s = NULL;
        int rc = aether_b0_source_create_v2(200, 100, kHtml, strlen(kHtml), &o, &s, &d);
        require(rc == AETHER_ERROR_INVALID_ARGUMENT, "F images NULL con count 1");
    }

    free(regular);
    free(bold);
    if (failures) {
        fprintf(stderr, "b0 source v2: %d fallos\n", failures);
        return 1;
    }
    puts("B0 SOURCE V2 PASS");
    return 0;
}
