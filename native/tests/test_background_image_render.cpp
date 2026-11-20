// Ciclo 01: background-image en el renderer propio (§3.1 del plan).
// Snapshot a mano (comandos prep::Image con DecodedImage reales) -> prepare
// -> software::render sobre FrameStore (patrón g1/g2). Píxeles BGRA8.
#ifdef NDEBUG
#error Assertions required
#endif
#include "background_image_fixtures.h"
#include "../../native/src/rendering/software/software_renderer.h"
#include <aether/aether.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>
namespace p = aether::rendering::preparation;
namespace s = aether::rendering::software;
using namespace aether::rendering;

static unsigned tbyte(double v) { return unsigned(std::floor(v * 255 + 0.5)); }
static unsigned tmul(unsigned a, unsigned b) { return (a * b + 127) / 255; }

static std::shared_ptr<const p::DecodedImage> decoded(const unsigned char* bytes, std::size_t n) {
    int32_t w = 0, h = 0;
    uint8_t* px = nullptr;
    assert(aether_image_decode(bytes, n, &w, &h, &px) == AETHER_OK);
    assert(px && w >= 1 && h >= 1);
    auto img = std::make_shared<p::DecodedImage>();
    img->width = w;
    img->height = h;
    img->pixels.assign(px, px + std::size_t(w) * std::size_t(h) * 4);
    aether_image_pixels_free(px);
    return img;
}
static p::Image image(float dx, float dy, float dw, float dh, float sx, float sy, float sw, float sh,
                      float opacity, std::shared_ptr<const p::DecodedImage> img) {
    p::Image c;
    c.destination = {dx, dy, dw, dh};
    c.source = {sx, sy, sw, sh};
    c.opacity = opacity;
    c.image = std::move(img);
    return c;
}
static p::PreparedFrame prep(std::vector<p::Command> c) {
    auto f = p::prepare({std::move(c), false});
    assert(std::holds_alternative<p::PreparedFrame>(f));
    return std::get<p::PreparedFrame>(std::move(f));
}
// Oráculo bilineal independiente (§2.4.4), escrito desde la fórmula del plan,
// no desde el kernel: muestrea texels premultiplicados BGRA y compone
// source-over con byte()/mul() propios.
static void expect_bilinear(const std::vector<uint8_t>& pixels, int width, int x, int y,
                            const p::DecodedImage& decoded_img, double orig_x, double orig_y,
                            double orig_w, double orig_h, double src_x, double src_y,
                            double src_w, double src_h, double opacity) {
    const double u = src_x + (double(x) + 0.5 - orig_x) * src_w / orig_w;
    const double v = src_y + (double(y) + 0.5 - orig_y) * src_h / orig_h;
    const double fx = u - 0.5, fy = v - 0.5;
    int i0 = int(std::floor(fx)), j0 = int(std::floor(fy));
    const double t = fx - i0, s = fy - j0;
    int i1 = i0 + 1, j1 = j0 + 1;
    i0 = std::clamp(i0, 0, int(decoded_img.width) - 1);
    i1 = std::clamp(i1, 0, int(decoded_img.width) - 1);
    j0 = std::clamp(j0, 0, int(decoded_img.height) - 1);
    j1 = std::clamp(j1, 0, int(decoded_img.height) - 1);
    auto texel = [&](int ix, int iy) {
        return decoded_img.pixels.data() + (std::size_t(iy) * std::size_t(decoded_img.width) + std::size_t(ix)) * 4;
    };
    const auto* p00 = texel(i0, j0);
    const auto* p10 = texel(i1, j0);
    const auto* p01 = texel(i0, j1);
    const auto* p11 = texel(i1, j1);
    double sample[4]{};
    for (int c = 0; c < 4; ++c)
        sample[c] = (1 - t) * (1 - s) * p00[c] + t * (1 - s) * p10[c] + (1 - t) * s * p01[c] + t * s * p11[c];
    const unsigned sA = tbyte(sample[3] / 255.0 * opacity);
    const unsigned inv = 255 - sA;
    const auto* p = pixels.data() + (std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4;
    for (int c = 0; c < 3; ++c)
        assert(p[c] == std::min(sA, tbyte(sample[c] / 255.0 * opacity)) + tmul(p[c], inv));
    assert(p[3] == sA + tmul(p[3], inv));
}
static std::vector<uint8_t> render_pixels(int width, int height, std::vector<p::Command> commands,
                                          s::Result* out_result = nullptr) {
    FrameStore store;
    assert(store.resize({width, height, 1}) == FrameStatus::ok);
    auto frame = prep(std::move(commands));
    auto result = s::render(store, frame);
    assert(result.status == s::Status::ok);
    if (out_result) *out_result = result;
    std::optional<Frame> f;
    assert(store.copy_confirmed(f) == FrameStatus::ok && f);
    return f->pixels;
}
static void expect_pixel(const std::vector<uint8_t>& pixels, int width, int x, int y,
                         uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
    const auto* p = pixels.data() + (std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4;
    assert(p[0] == b && p[1] == g && p[2] == r && p[3] == a);
}

int main() {
    const auto red1 = decoded(kRed1Png, sizeof(kRed1Png));
    const auto rgbw2 = decoded(kRgbw2Png, sizeof(kRgbw2Png));
    const auto alpha2 = decoded(kAlpha2Png, sizeof(kAlpha2Png));
    const auto checker4 = decoded(kChecker4Png, sizeof(kChecker4Png));
    // El fixture lleva RGBA (0,0,255,128) sin premultiplicar; el decode lo deja
    // en BGRA premultiplicado (128,0,0,128) (m8).
    assert(alpha2->pixels[0] == 128 && alpha2->pixels[1] == 0 && alpha2->pixels[2] == 0 &&
           alpha2->pixels[3] == 128);

    // K1: 1x1 rojo, caja 100x40, no-repeat, auto, pos 0,0.
    {
        s::Result result{};
        auto px = render_pixels(100, 40, {image(0, 0, 1, 1, 0, 0, 1, 1, 1, red1)}, &result);
        expect_pixel(px, 100, 0, 0, 0, 0, 255, 255);
        expect_pixel(px, 100, 50, 20, 0, 0, 0, 0);
        assert(result.image_uses == 1 && result.image_bytes == 4);
    }
    // K2: rgbw2 2x2, caja 8x8, repeat, tile 2x2 (16 tiles).
    {
        std::vector<p::Command> commands;
        for (int ty = 0; ty < 4; ++ty)
            for (int tx = 0; tx < 4; ++tx)
                commands.push_back(image(float(tx * 2), float(ty * 2), 2, 2, 0, 0, 2, 2, 1, rgbw2));
        auto px = render_pixels(8, 8, std::move(commands));
        expect_pixel(px, 8, 2, 0, 0, 0, 255, 255); // texel (0,0) rojo
        expect_pixel(px, 8, 3, 1, 255, 255, 255, 255); // texel (1,1) blanco
        expect_pixel(px, 8, 0, 0, 0, 0, 255, 255);
        expect_pixel(px, 8, 4, 0, 0, 0, 255, 255); // periodicidad exacta cada 2 px
        expect_pixel(px, 8, 6, 0, 0, 0, 255, 255);
    }
    // K3: rgbw2 escalado a tile 4x4 en caja 4x4 (§7 caso B).
    {
        auto px = render_pixels(4, 4, {image(0, 0, 4, 4, 0, 0, 2, 2, 1, rgbw2)});
        expect_pixel(px, 4, 0, 0, 0, 0, 255, 255);
        expect_pixel(px, 4, 1, 1, 64, 64, 159, 255); // bilineal 0.75/0.25
        expect_pixel(px, 4, 3, 3, 255, 255, 255, 255);
        expect_bilinear(px, 4, 1, 1, *rgbw2, 0, 0, 4, 4, 0, 0, 2, 2, 1);
        expect_bilinear(px, 4, 2, 1, *rgbw2, 0, 0, 4, 4, 0, 0, 2, 2, 1);
    }
    // K4: texel 50% alfa sobre fondo sólido #123456 pintado antes.
    {
        p::Fill bg{{0, 0, 4, 4}, {}, {0x12 / 255.f, 0x34 / 255.f, 0x56 / 255.f, 1}};
        auto px = render_pixels(4, 4, {bg, image(0, 0, 2, 2, 0, 0, 2, 2, 1, alpha2)});
        // Valor desde la fórmula, no desde el kernel: sA=128, inv=127.
        const unsigned sA = tbyte(128 / 255.0), inv = 255 - sA;
        const uint8_t b = uint8_t(std::min(sA, tbyte(128 / 255.0)) + tmul(0x56, inv));
        const uint8_t g = uint8_t(tmul(0x34, inv));
        const uint8_t r = uint8_t(tmul(0x12, inv));
        const uint8_t a = uint8_t(sA + tmul(255, inv));
        expect_pixel(px, 4, 0, 0, b, g, r, a);
        assert(b == 171 && g == 26 && r == 9 && a == 255);
    }
    // K5: red1, opacidad de capa 0.5, caja 10x10 sobre negro transparente (§7 D).
    {
        auto px = render_pixels(10, 10, {image(0, 0, 10, 10, 0, 0, 1, 1, 0.5f, red1)});
        expect_pixel(px, 10, 5, 5, 0, 0, 128, 128);
    }
    // K6: rgbw2 con size 3.5x3.5 en caja 7x7: borde con cobertura parcial.
    {
        auto px = render_pixels(7, 7, {image(0, 0, 3.5f, 3.5f, 0, 0, 2, 2, 1, rgbw2)});
        const auto* edge = px.data() + (std::size_t(1) * 7 + 3) * 4;
        assert(edge[3] > 0 && edge[3] < 255); // píxel (3,1): cobertura 0.5
        const auto* center = px.data() + (std::size_t(1) * 7 + 1) * 4;
        assert(center[3] == 255); // centro opaco
    }
    // K7: PushClip radio 8 sobre caja 20x20 + imagen debajo.
    {
        aether_corner_radii radii{8, 8, 8, 8, 8, 8, 8, 8};
        auto px = render_pixels(20, 20, {p::PushClip{{0, 0, 20, 20}, radii},
                                          image(0, 0, 20, 20, 0, 0, 1, 1, 1, red1), p::PopClip{}});
        expect_pixel(px, 20, 0, 0, 0, 0, 0, 0); // esquina fuera del clip curvo
        expect_pixel(px, 20, 10, 10, 0, 0, 255, 255); // centro = imagen
    }
    // K8: BeginOpacity(0.5) + imagen + EndOpacity.
    {
        auto px = render_pixels(10, 10, {p::BeginOpacity{1, 0, .5f},
                                          image(0, 0, 10, 10, 0, 0, 1, 1, 1, red1), p::EndOpacity{1}});
        // Grupo: capa roja opaca compuesta con opacidad 0.5 sobre transparente.
        expect_pixel(px, 10, 5, 5, 0, 0, 128, 128);
    }
    // K9: rgbw2 a tile 4x4 con repeat en caja 8x4: costura entre tiles (m1).
    {
        auto px = render_pixels(8, 4, {image(0, 0, 4, 4, 0, 0, 2, 2, 1, rgbw2),
                                        image(4, 0, 4, 4, 0, 0, 2, 2, 1, rgbw2)});
        expect_pixel(px, 8, 3, 0, 0, 255, 0, 255); // verde: último texel del tile 1, clamp
        expect_pixel(px, 8, 4, 0, 0, 0, 255, 255); // rojo: primer texel del tile 2, clamp
    }
    // R7: destination_rect con ancho 0 -> INVALID_PAYLOAD en validate_command.
    {
        auto bad = image(0, 0, 0, 4, 0, 0, 2, 2, 1, rgbw2);
        auto result = p::prepare({{bad}, false});
        assert(std::holds_alternative<p::Error>(result));
        assert(std::get<p::Error>(result).code == p::ErrorCode::INVALID_PAYLOAD);
    }
    // R8: Image con image == nullptr o pixels.size() != w*h*4 -> INVALID_PAYLOAD.
    {
        auto null_image = image(0, 0, 2, 2, 0, 0, 2, 2, 1, nullptr);
        auto result = p::prepare({{null_image}, false});
        assert(std::holds_alternative<p::Error>(result));
        assert(std::get<p::Error>(result).code == p::ErrorCode::INVALID_PAYLOAD);
        auto mismatch = std::make_shared<p::DecodedImage>();
        mismatch->width = 2;
        mismatch->height = 2;
        mismatch->pixels.assign(12, 0); // 12 != 2*2*4
        auto bad = image(0, 0, 2, 2, 0, 0, 2, 2, 1, mismatch);
        auto result2 = p::prepare({{bad}, false});
        assert(std::holds_alternative<p::Error>(result2));
        assert(std::get<p::Error>(result2).code == p::ErrorCode::INVALID_PAYLOAD);
    }
    // checker4 existe para escalado mayor; se usa en B5 (test B0), aquí solo
    // se verifica que decodifica a 4x4.
    assert(checker4->width == 4 && checker4->height == 4);
    std::cout << "background-image render K1-K9 R7-R8 PASS\n";
    return 0;
}
