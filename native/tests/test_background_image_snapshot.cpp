// Ciclo 01: background-image en snapshot (§3.2 del plan).
// Arnesa A: escena G1 (doc -> style -> layout -> builder -> snapshot).
// Arnesa B: lista construida a mano (precedente test_native_text_snapshot).
// Arnesa C: finalize directo (precedente test_source_rounded_policy).
// Uso: test [DejaVuSans.ttf] [DejaVuSans-Bold.ttf] (solo la arnesa C).
#ifdef NDEBUG
#error Assertions required
#endif
#include "background_image_fixtures.h"
#include "../src/rendering/preparation/native_command_source.h"
#include "../src/rendering/display_list_types.h"
#include "../src/rendering/html_ltr/image_registry.h"
#include "../src/rendering/html_ltr/paint_finalize.h"
#include "../src/rendering/software/software_renderer.h"
#include <aether/aether.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <vector>
namespace p = aether::rendering::preparation;
namespace s = aether::rendering::software;
using namespace aether::rendering;
using namespace aether::rendering::html_ltr;

static bool bad(const p::SnapshotResult& r, p::ErrorCode e = p::ErrorCode::INVALID_PAYLOAD) {
    auto* err = std::get_if<p::Error>(&r);
    return err && err->code == e;
}
static std::shared_ptr<const p::DecodedImage> decoded_red1() {
    int32_t w = 0, h = 0;
    uint8_t* px = nullptr;
    assert(aether_image_decode(kRed1Png, sizeof(kRed1Png), &w, &h, &px) == AETHER_OK);
    auto img = std::make_shared<p::DecodedImage>();
    img->width = w;
    img->height = h;
    img->pixels.assign(px, px + std::size_t(w) * std::size_t(h) * 4);
    aether_image_pixels_free(px);
    return img;
}
static std::vector<unsigned char> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    assert(f);
    return {std::istreambuf_iterator<char>(f), {}};
}

// ---- Arnesa A: escena G1 ----
aether_text_metrics_t measure(const char*, const aether_computed_style_t*, float, void*) {
    assert(false);
    return {};
}
struct Scene {
    aether_doc_t doc{};
    aether_style_map_t styles{};
    aether_style_runtime_t runtime{};
    aether_layout_engine_t engine{};
    aether_layout_result_t layout{};
    aether_display_list_builder_t builder{};
    std::unique_ptr<NativePaintList> native;
    explicit Scene(const char* css) {
        assert(aether_doc_create(&doc) == AETHER_OK);
        assert(aether_doc_parse_html(doc, "<body><div id='box'></div></body>") == AETHER_OK);
        aether_style_environment_t env{32, 24, 1, 16, 0, 0, 0};
        assert(aether_style_runtime_create(nullptr, &env, nullptr, nullptr, &runtime) == AETHER_OK);
        int64_t sheet{};
        int32_t rules{};
        assert(aether_style_runtime_add_stylesheet(runtime, css, 0, "bg-image-scene", &sheet, &rules) ==
               AETHER_OK);
        aether_style_update_t* updates{};
        int count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
        assert(count >= 0);
        aether_style_updates_free(updates, count);
        assert(aether_style_runtime_create_resolved_style_map(runtime, doc, &styles) == AETHER_OK);
        assert(aether_layout_create(measure, nullptr, &engine) == AETHER_OK);
        assert(aether_layout_layout(engine, doc, styles, 32, 24, nullptr, nullptr, &layout) == AETHER_OK);
        assert(aether_display_list_builder_create(measure, nullptr, &builder) == AETHER_OK);
    }
    auto build() {
        auto status = build_native_opacity(builder, layout, styles, nullptr, native);
        if (status != AETHER_OK) return p::SnapshotResult{p::Error{p::ErrorCode::UNSUPPORTED_TAG}};
        return p::snapshot_native_opacity(*native);
    }
    ~Scene() {
        native.reset();
        aether_display_list_builder_destroy(builder);
        aether_layout_result_destroy(layout);
        aether_layout_destroy(engine);
        aether_style_map_destroy(styles);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(doc);
    }
};
static aether_bool_t red1_dimensions(const char* source, aether_sizef* out, void* user_data) {
    (void)user_data;
    if (!source || !out || std::strcmp(source, "red1.png") != 0) return AETHER_FALSE;
    out->width = 1;
    out->height = 1;
    return AETHER_TRUE;
}

// ---- Arnesa B: lista a mano ----
static aether_paint_command_t image_command(const char* source, bool resolved, float opacity = 1,
                                            int32_t repeat_x = AETHER_BACKGROUND_NO_REPEAT,
                                            int32_t repeat_y = AETHER_BACKGROUND_NO_REPEAT,
                                            int32_t size_kind = AETHER_BACKGROUND_SIZE_AUTO) {
    aether_paint_command_t c{};
    c.tag = AETHER_PAINT_DRAW_BACKGROUND_IMAGE;
    auto& d = c.data.draw_background_image;
    d.source = source ? strdup(source) : nullptr;
    d.paint_rect = {0, 0, 16, 12};
    d.size_kind = size_kind;
    d.repeat_x = repeat_x;
    d.repeat_y = repeat_y;
    d.opacity = opacity;
    d.source_rect = {0, 0, 2, 2};
    d.destination_rect = {0, 0, 2, 2};
    d.has_resolved_geometry = resolved ? AETHER_TRUE : AETHER_FALSE;
    return c;
}
static std::shared_ptr<const p::DecodedImage> big_image(uint8_t fill) {
    auto img = std::make_shared<p::DecodedImage>();
    img->width = 2048;
    img->height = 2048;
    img->pixels.assign(std::size_t(2048) * std::size_t(2048) * 4, fill);
    return img;
}

int main(int argc, char** argv) {
    assert(argc == 3);
    const auto red1 = decoded_red1();
    const std::string css =
        "body{display:block;margin:0}div{display:block}"
        "#box{width:16px;height:12px;background-image:url('red1.png')}";

    // R1: sin callback de dimensiones -> comando declarativo sin resolver ->
    // snapshot UNSUPPORTED_TAG; frame confirmado intacto.
    {
        FrameStore store;
        assert(store.resize({32, 24, 1}) == FrameStatus::ok);
        auto empty = p::prepare({{}, false});
        assert(std::holds_alternative<p::PreparedFrame>(empty));
        assert(s::render(store, std::get<p::PreparedFrame>(empty)).status == s::Status::ok);
        std::optional<Frame> before;
        assert(store.copy_confirmed(before) == FrameStatus::ok && before);
        Scene scene(css.c_str());
        auto result = scene.build();
        assert(bad(result, p::ErrorCode::UNSUPPORTED_TAG));
        std::optional<Frame> after;
        assert(store.copy_confirmed(after) == FrameStatus::ok && after);
        assert(after->sequence == before->sequence && after->pixels == before->pixels);
    }
    // R2: con callback de dimensiones (resuelto) pero sin attach() -> el
    // sidecar está vacío -> snapshot UNSUPPORTED_TAG (regla 2).
    {
        Scene scene(css.c_str());
        aether_display_list_builder_set_image_dimensions_callback(scene.builder, red1_dimensions, nullptr);
        auto result = scene.build();
        assert(bad(result, p::ErrorCode::UNSUPPORTED_TAG));
    }
    // Caso de éxito (§4.3, D2): resuelto + sidecar -> Snapshot con el comando
    // Image y command_tag == AETHER_PAINT_DRAW_BACKGROUND_IMAGE. El equivalente
    // no cabe en test_aether_command_source.cpp (lista falsa + namespace avc),
    // así que vive aquí, en el arnés que sí usa snapshot_native_opacity real.
    {
        NativePaintList n;
        NativeListAccess::list(n) = new aether_display_list_s;
        NativeListAccess::list(n)->commands.push_back(image_command("red1.png", true));
        n.image_runs.push_back({0, red1});
        auto result = p::snapshot_native_opacity(n);
        assert(std::holds_alternative<p::Snapshot>(result));
        const auto& snap = std::get<p::Snapshot>(result);
        assert(snap.commands.size() == 1);
        const auto* img = std::get_if<p::Image>(&snap.commands[0]);
        assert(img && img->image == red1);
        assert(img->destination.x == 0 && img->destination.y == 0 && img->destination.width == 2 &&
               img->destination.height == 2);
        assert(p::command_tag(snap.commands[0]) == AETHER_PAINT_DRAW_BACKGROUND_IMAGE);
        assert(std::holds_alternative<p::PreparedFrame>(p::prepare(snap)));
    }
    // R5: enums/opacidad/rect inválidos con entrada de sidecar (regla 3).
    {
        auto expect_invalid = [&](aether_paint_command_t cmd) {
            NativePaintList n;
            NativeListAccess::list(n) = new aether_display_list_s;
            NativeListAccess::list(n)->commands.push_back(cmd);
            n.image_runs.push_back({0, red1});
            assert(bad(p::snapshot_native_opacity(n), p::ErrorCode::INVALID_PAYLOAD));
        };
        expect_invalid(image_command("red1.png", true, 1, 99));
        expect_invalid(image_command("red1.png", true, 1, AETHER_BACKGROUND_NO_REPEAT,
                                     AETHER_BACKGROUND_NO_REPEAT, 99));
        expect_invalid(image_command("red1.png", true, 1.5f));
        auto nan_rect = image_command("red1.png", true);
        nan_rect.data.draw_background_image.destination_rect.width =
            std::numeric_limits<float>::quiet_NaN();
        expect_invalid(nan_rect);
    }
    // R4: 5 imágenes distintas de 16 MiB exactos (80 MiB) -> LIMIT_EXCEEDED;
    // el prefijo de 4 (64 MiB exactos) pasa snapshot.
    {
        NativePaintList n;
        NativeListAccess::list(n) = new aether_display_list_s;
        for (int i = 0; i < 5; ++i) {
            NativeListAccess::list(n)->commands.push_back(
                image_command("red1.png", true, 1, AETHER_BACKGROUND_NO_REPEAT,
                              AETHER_BACKGROUND_NO_REPEAT, AETHER_BACKGROUND_SIZE_EXPLICIT));
            n.image_runs.push_back({std::size_t(i), big_image(uint8_t(i + 1))});
        }
        assert(bad(p::snapshot_native_opacity(n), p::ErrorCode::LIMIT_EXCEEDED));
        NativePaintList four;
        NativeListAccess::list(four) = new aether_display_list_s;
        for (int i = 0; i < 4; ++i) {
            NativeListAccess::list(four)->commands.push_back(image_command("red1.png", true));
            four.image_runs.push_back({std::size_t(i), big_image(uint8_t(i + 1))});
        }
        auto ok = p::snapshot_native_opacity(four);
        assert(std::holds_alternative<p::Snapshot>(ok));
    }
    // R3: PNG con IHDR 5000x5000 (100 MiB decodificados) -> LIMIT_EXCEEDED en
    // add() sin asignar píxeles; find() devuelve nulo después.
    {
        ImageRegistry registry;
        assert(registry.add("huge.png", kHugeHeaderPng, sizeof(kHugeHeaderPng)) ==
               AETHER_ERROR_LIMIT_EXCEEDED);
        assert(!registry.find("huge.png"));
        assert(registry.add("red1.png", kRed1Png, sizeof(kRed1Png)) == AETHER_OK);
        assert(registry.find("red1.png") != nullptr);
        assert(registry.find("red1.png")->width == 1 && registry.find("red1.png")->height == 1);
    }
    // R9: comando sin resolver -> finalize_paint NO lanza (§2.7.3 lo salta);
    // snapshot_native_opacity -> UNSUPPORTED_TAG (fija el orden, M5 Rev 3).
    {
        FontProvider fonts(read_file(argv[1]), read_file(argv[2]), 2);
        NativePaintList n;
        NativeListAccess::list(n) = new aether_display_list_s;
        aether_paint_command_t c{};
        c.tag = AETHER_PAINT_DRAW_BACKGROUND_IMAGE; // a cero: sin resolver, source NULL
        NativeListAccess::list(n)->commands.push_back(c);
        bool finalized = false;
        try {
            const auto masks = finalize_paint(n, {}, fonts, {}, 2, "AetherB0", BoxPaintPolicy::source_rounded);
            assert(masks.empty());
            finalized = true;
        } catch (const Error&) {
            finalized = false;
        }
        assert(finalized);
        assert(bad(p::snapshot_native_opacity(n), p::ErrorCode::UNSUPPORTED_TAG));
    }
    std::cout << "background-image snapshot R1 R2 R3 R4 R5 R9 PASS\n";
    return 0;
}
