// Ciclo 01: background-image en la sesión B0 (§3.3 del plan).
// B0SourceSession directa (C++), con set_image_registry antes de prepare(),
// render con render_a8 + masks() (patrón aether_b0_source.cpp).
// Uso: test [DejaVuSans.ttf] [DejaVuSans-Bold.ttf]. Viewport B1-B5: 200x100.
#ifdef NDEBUG
#error Assertions required
#endif
#include "background_image_fixtures.h"
#include "../src/rendering/html_ltr/b0_source_session.h"
#include "../src/rendering/html_ltr/image_registry.h"
#include "../src/rendering/software/software_renderer.h"
#include <aether/aether.h>
#include <aether/aether_b0_source.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
namespace s = aether::rendering::software;
using namespace aether::rendering;
using namespace aether::rendering::html_ltr;

static std::vector<unsigned char> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    assert(f);
    return {std::istreambuf_iterator<char>(f), {}};
}
static std::string page(const std::string& box_css) {
    return "<!doctype html><html lang=\"es\"><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
           "<title>bg image</title><style>"
           "html{font-family:AetherB0;font-size:16px;font-weight:400;line-height:24px;"
           "color:#202020;white-space:nowrap}"
           "body{display:block}"
           ".box{display:block;" +
           box_css +
           "}</style></head><body><div class=\"box\"></div></body></html>";
}
static std::shared_ptr<const ImageRegistry> registry_with(const char* name, const unsigned char* bytes,
                                                          std::size_t n) {
    auto registry = std::make_shared<ImageRegistry>();
    assert(registry->add(name, bytes, n) == AETHER_OK);
    return registry;
}
struct Rendered {
    std::vector<uint8_t> pixels;
    uint64_t sequence = 0;
};
static Rendered render_session(B0SourceSession& session) {
    const auto& frame = session.prepare();
    FrameStore store;
    assert(store.resize({200, 100, 1}) == FrameStatus::ok);
    auto result = s::render_a8(store, frame, session.masks(), nullptr);
    assert(result.status == s::Status::ok);
    std::optional<Frame> f;
    assert(store.copy_confirmed(f) == FrameStatus::ok && f);
    return {f->pixels, f->sequence};
}
static void expect_pixel(const std::vector<uint8_t>& pixels, int x, int y, uint8_t b, uint8_t g, uint8_t r,
                         uint8_t a) {
    const auto* p = pixels.data() + (std::size_t(y) * 200 + std::size_t(x)) * 4;
    assert(p[0] == b && p[1] == g && p[2] == r && p[3] == a);
}

int main(int argc, char** argv) {
    assert(argc == 3);
    const auto regular = read_file(argv[1]);
    const auto bold = read_file(argv[2]);

    // B1: div 100x40 con background-image: url('red1.png') + registro -> pinta.
    {
        B0SourceSession session(page("width:100px;padding-top:20px;padding-bottom:20px;background-image:url('red1.png');background-repeat:no-repeat"),
                                regular, bold, Viewport{200, 100, 1});
        session.set_image_registry(registry_with("red1.png", kRed1Png, sizeof(kRed1Png)));
        auto rendered = render_session(session);
        assert(rendered.sequence == 1);
        expect_pixel(rendered.pixels, 0, 0, 0, 0, 255, 255);
        expect_pixel(rendered.pixels, 50, 20, 0, 0, 0, 0); // fuera del tile 1x1
    }
    // B2: misma página sin registrar la fuente -> UNSUPPORTED / PREPARATION (8)
    // / UNSUPPORTED con command_index del comando DRAW_BACKGROUND_IMAGE (M2, §2.6.5).
    {
        B0SourceSession session(page("width:100px;padding-top:20px;padding-bottom:20px;background-image:url('red1.png');background-repeat:no-repeat"),
                                regular, bold, Viewport{200, 100, 1});
        bool thrown = false;
        try {
            (void)session.prepare();
        } catch (const B0SourceError& e) {
            thrown = true;
            assert(e.status == AETHER_ERROR_UNSUPPORTED);
            assert(e.stage == AETHER_B0_STAGE_PREPARATION);
            assert(e.reason == AETHER_B0_REASON_UNSUPPORTED);
            // Índice exacto del DRAW_BACKGROUND_IMAGE en la lista de esta
            // página: [BEGIN_CHUNK, BEGIN_CHUNK, PUSH_CLIP, IMAGE, POP_CLIP,
            // END_CHUNK, END_CHUNK] (verificado con sonda sobre el builder).
            assert(e.command_index == 3);
        }
        assert(thrown);
    }
    // B3: dos capas -> rechazo en la puerta del compilador (§2.7.1, un solo
    // token): etapa CSS, CSS_LOSS. El count == 1 de §2.7.2 es defensa en
    // profundidad, no el mecanismo que rechaza B3 (m1).
    {
        B0SourceSession session(
            page("width:100px;padding-top:20px;padding-bottom:20px;background-image:url('red1.png'), url('rgbw2.png')"), regular,
            bold, Viewport{200, 100, 1});
        session.set_image_registry(registry_with("red1.png", kRed1Png, sizeof(kRed1Png)));
        bool thrown = false;
        try {
            (void)session.prepare();
        } catch (const B0SourceError& e) {
            thrown = true;
            assert(e.status == AETHER_ERROR_UNSUPPORTED);
            assert(e.stage == AETHER_B0_STAGE_CSS);
            assert(e.reason == AETHER_B0_REASON_CSS_LOSS);
        }
        assert(thrown);
    }
    // B4: border-radius 8px + imagen -> rechazo en la puerta de estilo
    // (§2.7.2, etapa STYLE: radio 0 tras el bucle de radios, m2; no en
    // validate_controls, m7).
    {
        B0SourceSession session(
            page("width:100px;padding-top:20px;padding-bottom:20px;border-radius:8px;background-image:url('red1.png')"), regular,
            bold, Viewport{200, 100, 1});
        session.set_image_registry(registry_with("red1.png", kRed1Png, sizeof(kRed1Png)));
        bool thrown = false;
        try {
            (void)session.prepare();
        } catch (const B0SourceError& e) {
            thrown = true;
            assert(e.status == AETHER_ERROR_UNSUPPORTED);
            assert(e.stage == AETHER_B0_STAGE_STYLE);
            assert(e.reason == AETHER_B0_REASON_UNSUPPORTED);
        }
        assert(thrown);
    }
    // B5: background-size: cover + background-repeat: repeat con rgbw2 2x2 en
    // caja 40x40 -> píxeles contra geometría + bilineal.
    {
        B0SourceSession session(
            page("width:40px;padding-top:20px;padding-bottom:20px;background-image:url('rgbw2.png');"
                 "background-size:cover;background-repeat:repeat"),
            regular, bold, Viewport{200, 100, 1});
        session.set_image_registry(registry_with("rgbw2.png", kRgbw2Png, sizeof(kRgbw2Png)));
        auto rendered = render_session(session);
        expect_pixel(rendered.pixels, 0, 0, 0, 0, 255, 255); // clamp a texel (0,0)
        expect_pixel(rendered.pixels, 39, 39, 255, 255, 255, 255); // clamp a (1,1)
        // Centro (20,20): u=v=1.025 -> t=s=0.525 -> mezcla de los 4 texels.
        // B=G=(0.249375+0.275625)*255=133.875->134; R=(0.225625+0.275625)*255=127.8->128.
        expect_pixel(rendered.pixels, 20, 20, 134, 134, 128, 255);
    }
    std::cout << "b0 background-image B1-B5 PASS\n";
    return 0;
}
