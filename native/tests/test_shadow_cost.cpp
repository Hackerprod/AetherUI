/* Ciclo 04 (§3.5, §4.2): coste y píxeles del kernel para box-shadow (K1–K6).
 * Modelo: native/tests/test_gradient_cost.cpp (prepare/render), pero SIN
 * assert() y sin #error bajo NDEBUG: debe pasar en Release (patrón
 * require() + exit(1)).
 * Compila las fuentes internas en el binario (patrón test_gradient_angle_runtime):
 * el test + software_renderer.cpp + native_frame.cpp + las 3 fuentes de
 * preparación que usa prepare(Snapshot) (aether_command_source.cpp,
 * paint_snapshot.cpp, preparation_metrics.cpp; native_command_source.cpp
 * referencia símbolos ABI que solo existen en Core — desviación D1 del
 * ciclo 03). Sin enlazar AetherUI::Core.
 * Presupuesto por sombra (§2.3): (columns+rows)·256 + píxeles·2 con
 * r = intersect(viewport, support); clear = viewport×1; Fill = count×1.
 * BGRA en las aserciones de píxel. */
#include "../src/rendering/software/software_renderer.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <vector>

using namespace aether::rendering;
namespace p = aether::rendering::preparation;
namespace s = aether::rendering::software;

namespace {
size_t checks = 0;
void require(bool condition, const char* detail) {
  ++checks;
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s (check %zu)\n", detail, checks);
    std::exit(1);
  }
}

p::PreparedFrame prepare(std::vector<p::Command> commands) {
  auto r = p::prepare(p::Snapshot{std::move(commands)});
  require(std::holds_alternative<p::PreparedFrame>(r), "prepare ok");
  return std::move(std::get<p::PreparedFrame>(r));
}

Frame get(FrameStore& store) {
  std::optional<Frame> out;
  require(store.copy_confirmed(out) == FrameStatus::ok, "copy_confirmed ok");
  require(out.has_value(), "confirmed frame present");
  return *out;
}

std::array<int, 4> pixel(const Frame& f, int x, int y) {
  const auto n = std::size_t(y) * f.stride + std::size_t(x) * 4;
  return {f.pixels[n], f.pixels[n + 1], f.pixels[n + 2], f.pixels[n + 3]};
}

void require_pixel(FrameStore& store, int x, int y, std::array<int, 4> want, const char* label) {
  ++checks;
  const auto got = pixel(get(store), x, y);
  if (got != want) {
    std::fprintf(stderr, "FAIL %s: pixel (%d,%d) = (%d,%d,%d,%d), want (%d,%d,%d,%d)\n",
                 label, x, y, got[0], got[1], got[2], got[3], want[0], want[1], want[2], want[3]);
    std::exit(1);
  }
}

void require_budget(const s::Result& r, std::uint64_t want, const char* label) {
  ++checks;
  if (r.status != s::Status::ok || r.pixel_budget != want) {
    std::fprintf(stderr, "FAIL %s: status=%d budget=%llu want=%llu\n", label, int(r.status),
                 (unsigned long long)r.pixel_budget, (unsigned long long)want);
    std::exit(1);
  }
}

void require_unsupported(FrameStore& store, std::vector<p::Command> commands, const char* label) {
  ++checks;
  auto r = s::render(store, prepare(std::move(commands)));
  if (r.status != s::Status::unsupported) {
    std::fprintf(stderr, "FAIL %s: status=%d want unsupported\n", label, int(r.status));
    std::exit(1);
  }
}

p::Radii zero_radii() { return {0, 0, 0, 0, 0, 0, 0, 0}; }

p::Color bg() { return {244.0f / 255, 246.0f / 255, 250.0f / 255, 1.0f}; }
p::Color black() { return {0, 0, 0, 1}; }
p::Color red() { return {1, 0, 0, 1}; }

p::Fill fill(p::Rect rect, p::Color c) { return {rect, zero_radii(), c}; }

p::BoxShadow shadow(float dx, float dy, float blur, float spread, p::Color c,
                    aether_bool_t inset = AETHER_FALSE) {
  p::BoxShadow s{};
  s.rect = {40, 40, 160, 100};
  s.radii = zero_radii();
  s.shadow.offset_x = dx;
  s.shadow.offset_y = dy;
  s.shadow.blur_radius = blur;
  s.shadow.spread_radius = spread;
  s.shadow.color = c;
  s.shadow.inset = inset;
  return s;
}
}  // namespace

int main() {
  FrameStore store;
  require(store.resize({400, 300, 1}) == FrameStatus::ok, "resize 400x300");
  /* K1: blur 0. 338560 = 120000 (clear) + 120000 (Fill) + 98560
   * ((160+100)·256 + 16000·2). */
  {
    auto r = s::render(store, prepare({fill({0, 0, 400, 300}, bg()), shadow(4, 4, 0, 0, black())}));
    require_budget(r, 338560, "K1 budget");
    require_pixel(store, 200, 100, {0, 0, 0, 255}, "K1 (200,100)");
    require_pixel(store, 203, 100, {0, 0, 0, 255}, "K1 (203,100)");
    require_pixel(store, 204, 100, {250, 246, 244, 255}, "K1 (204,100)");
    require_pixel(store, 44, 140, {0, 0, 0, 255}, "K1 (44,140)");
    require_pixel(store, 44, 144, {250, 246, 244, 255}, "K1 (44,144)");
  }
  /* K2: blur 8. 373632 = 120000 + 120000 + 133632
   * ((192+132)·256 + 25344·2; support (28,28,220,160)). */
  {
    auto r = s::render(store, prepare({fill({0, 0, 400, 300}, bg()), shadow(4, 4, 8, 0, black())}));
    require_budget(r, 373632, "K2 budget");
    require_pixel(store, 200, 100, {48, 47, 47, 255}, "K2 (200,100)");
    require_pixel(store, 204, 100, {137, 135, 134, 255}, "K2 (204,100)");
    require_pixel(store, 208, 100, {218, 214, 212, 255}, "K2 (208,100)");
    require_pixel(store, 44, 150, {243, 239, 237, 255}, "K2 (44,150)");
  }
  /* K3: spread 2. 342720 = 120000 + 120000 + 102720
   * ((164+104)·256 + 17056·2; source (42,42,206,146)). */
  {
    auto r = s::render(store, prepare({fill({0, 0, 400, 300}, bg()), shadow(4, 4, 0, 2, black())}));
    require_budget(r, 342720, "K3 budget");
    require_pixel(store, 205, 100, {0, 0, 0, 255}, "K3 (205,100)");
    require_pixel(store, 206, 100, {250, 246, 244, 255}, "K3 (206,100)");
    require_pixel(store, 42, 145, {0, 0, 0, 255}, "K3 (42,145)");
    require_pixel(store, 42, 146, {250, 246, 244, 255}, "K3 (42,146)");
  }
  /* K4: dos sombras. 437120 = 120000 + 120000 + 98560 + 98560. La roja
   * (offset -4,-4) se pinta primero; la negra después. */
  {
    auto r = s::render(store, prepare({fill({0, 0, 400, 300}, bg()), shadow(-4, -4, 0, 0, red()),
                                       shadow(4, 4, 0, 0, black())}));
    require_budget(r, 437120, "K4 budget");
    require_pixel(store, 36, 100, {0, 0, 255, 255}, "K4 (36,100) roja");
    require_pixel(store, 200, 100, {0, 0, 0, 255}, "K4 (200,100) negra");
  }
  /* K5: topes de blur. Blur 1 → 342720 (mismo cargo que spread 2 por
   * coincidencia: ambos expanden el support 2 px por lado, pero los
   * píxeles difieren). Blur 64 → 575232 = 120000 + 120000 + 335232
   * ((332+272)·256 + 90304·2; r = intersect(viewport, support) =
   * (0,0,332,272)). Solo prepare/render para los rechazos: blur 0.5,
   * blur 65, offset 8193, spread 8193 e inset → unsupported. */
  {
    auto r = s::render(store, prepare({fill({0, 0, 400, 300}, bg()), shadow(4, 4, 1, 0, black())}));
    require_budget(r, 342720, "K5 blur 1 budget");
    require_pixel(store, 203, 100, {49, 48, 48, 255}, "K5 blur 1 (203,100)");
    require_pixel(store, 204, 100, {201, 198, 196, 255}, "K5 blur 1 (204,100)");
    require_pixel(store, 205, 100, {249, 245, 243, 255}, "K5 blur 1 (205,100)");
  }
  {
    auto r = s::render(store, prepare({fill({0, 0, 400, 300}, bg()), shadow(4, 4, 64, 0, black())}));
    require_budget(r, 575232, "K5 blur 64 budget");
    require_pixel(store, 200, 100, {131, 129, 128, 255}, "K5 blur 64 (200,100)");
    require_pixel(store, 0, 0, {248, 244, 242, 255}, "K5 blur 64 (0,0)");
  }
  require_unsupported(store, {fill({0, 0, 400, 300}, bg()), shadow(4, 4, 0.5f, 0, black())},
                      "K5 blur 0.5");
  require_unsupported(store, {fill({0, 0, 400, 300}, bg()), shadow(4, 4, 65, 0, black())},
                      "K5 blur 65");
  require_unsupported(store, {fill({0, 0, 400, 300}, bg()), shadow(8193, 4, 0, 0, black())},
                      "K5 offset 8193");
  require_unsupported(store, {fill({0, 0, 400, 300}, bg()), shadow(4, 4, 0, 8193, black())},
                      "K5 spread 8193");
  require_unsupported(store, {fill({0, 0, 400, 300}, bg()), shadow(4, 4, 0, 0, black(), AETHER_TRUE)},
                      "K5 inset");
  /* K6: alfa 0.5. 338560 = 120000 + 120000 + 98560. */
  {
    p::Color half = {0, 0, 0, 0.5f};
    auto r = s::render(store, prepare({fill({0, 0, 400, 300}, bg()), shadow(4, 4, 0, 0, half)}));
    require_budget(r, 338560, "K6 budget");
    require_pixel(store, 200, 100, {125, 123, 122, 255}, "K6 (200,100)");
  }
  std::printf("SHADOW COST PASS (%zu checks)\n", checks);
  return 0;
}
