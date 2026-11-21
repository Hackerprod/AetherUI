/* Ciclo 03 (§3.3, §4.2): coste y píxeles del kernel para gradientes (K1–K7).
 * Modelo: native/tests/test_software_renderer.cpp (prepare/render), pero
 * SIN assert() y sin #error bajo NDEBUG: debe pasar en Release
 * (patrón require() + exit(1)).
 * Compila las fuentes internas en el binario (patrón test_gradient_angle_runtime):
 * el test + software_renderer.cpp + native_frame.cpp + las 4 fuentes de
 * preparación. Sin enlazar AetherUI::Core.
 * Presupuesto por píxel (plan §2.2): interior 1 muestra, borde 256 muestras
 * (factor 2048). pixel_budget es el observable; pixel_count se deduce de él
 * (el presupuesto exacto fija la partición interior/borde).
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

p::Radii uniform_radii(float v) {
  return p::Radii{v, v, v, v, v, v, v, v};
}

std::vector<p::GradientStop> two_stops() {
  return {p::GradientStop{0.0f, p::Color{1.0f, 0.0f, 0.0f, 1.0f}},
          p::GradientStop{1.0f, p::Color{0.0f, 0.0f, 1.0f, 1.0f}}};
}

p::LinearGradientFill gradient(double x, double y, double w, double h, double dx, double dy,
                              std::vector<p::GradientStop> stops, double radius = 0) {
  p::LinearGradientFill g{};
  g.rect = p::Rect{float(x), float(y), float(w), float(h)};
  g.gradient_rect = g.rect;
  g.radii = uniform_radii(float(radius));
  g.direction_x = float(dx);
  g.direction_y = float(dy);
  g.opacity = 1.0f;
  g.stops = std::move(stops);
  return g;
}

p::LinearGradientFill gradient_3stops() {
  return gradient(0, 0, 200, 200, 1, 0,
                  {p::GradientStop{0.0f, p::Color{1.0f, 0.0f, 0.0f, 1.0f}},
                   p::GradientStop{0.503f, p::Color{0.0f, 1.0f, 0.0f, 1.0f}},
                   p::GradientStop{1.0f, p::Color{0.0f, 0.0f, 1.0f, 1.0f}}});
}
}  // namespace

int main() {
  FrameStore store;
  require(store.resize({400, 300, 1}) == FrameStatus::ok, "resize 400x300");
  /* K1: 2 paradas, 200x200. 160000 = 120000 (clear) + 40000x1. */
  {
    auto r = s::render(store, prepare({gradient(0, 0, 200, 200, 1, 0, two_stops())}));
    require_budget(r, 160000, "K1 budget");
    require_pixel(store, 0, 0, {1, 0, 254, 255}, "K1 (0,0)");
    require_pixel(store, 100, 100, {128, 0, 127, 255}, "K1 (100,100)");
    require_pixel(store, 199, 199, {254, 0, 1, 255}, "K1 (199,199)");
  }
  /* K2: 3 paradas, 200x200. 569400 = 120000 + 39800x1 + 200x2048
   * (solo la columna x=100 tiene la parada 0.503 dentro de su rango-t:
   * t en [0.5025, 0.5075]; el resto es interior). */
  {
    auto r = s::render(store, prepare({gradient_3stops()}));
    require_budget(r, 569400, "K2 budget");
    require_pixel(store, 50, 100, {0, 128, 127, 255}, "K2 (50,100)");
    require_pixel(store, 150, 100, {128, 127, 0, 255}, "K2 (150,100)");
    require_pixel(store, 100, 100, {0, 254, 0, 255}, "K2 (100,100) borde");
  }
  /* K3: viewport 640x320. 409600 = 204800 (clear 640x320) + 204800x1. */
  {
    require(store.resize({640, 320, 1}) == FrameStatus::ok, "resize 640x320");
    auto r = s::render(store, prepare({gradient(0, 0, 640, 320, 1, 0, two_stops())}));
    require_budget(r, 409600, "K3 budget");
    require_pixel(store, 0, 0, {0, 0, 255, 255}, "K3 (0,0)");
    require_pixel(store, 319, 160, {127, 0, 128, 255}, "K3 (319,160)");
    require_pixel(store, 320, 0, {128, 0, 127, 255}, "K3 (320,0)");
    require_pixel(store, 639, 319, {255, 0, 0, 255}, "K3 (639,319)");
    require(store.resize({400, 300, 1}) == FrameStatus::ok, "resize 400x300 de nuevo");
  }
  /* K4: radio 10, 100x100. 948800 = 120000 + 9600x1 + 400x2048.
   * Banda central de 100x100 r10: 80x100 + 100x80 - 80x80 = 9600
   * píxeles interiores; los 400 restantes son las 4 esquinas 10x10. */
  {
    auto r = s::render(store, prepare({gradient(0, 0, 100, 100, 1, 0, two_stops(), 10)}));
    require_budget(r, 948800, "K4 budget");
    require_pixel(store, 50, 50, {129, 0, 126, 255}, "K4 (50,50)");
  }
  /* K5: clip curvo radio 10, 200x200. 1366000 = 120000 (clear)
   * + 858800 (gradiente: interior 39600x1 + borde 400x2048)
   * + 387200 (clip: 40000x2 + 400x768). Ver implementation.md: el
   * presupuesto (e) es derivado a mano, no confirmado por la sonda
   * (pre-cambio es limit_exceeded); el test K5 lo verifica. */
  {
    p::PushClip clip{};
    clip.rect = p::Rect{0, 0, 200, 200};
    clip.radii = uniform_radii(10);
    auto r = s::render(store, prepare({clip,
                                       gradient(0, 0, 200, 200, 1, 0, two_stops()),
                                       p::PopClip{}}));
    require_budget(r, 1366000, "K5 budget");
    require_pixel(store, 100, 100, {128, 0, 127, 255}, "K5 (100,100)");
    require_pixel(store, 50, 50, {64, 0, 191, 255}, "K5 (50,50)");
    require_pixel(store, 0, 0, {0, 0, 0, 0}, "K5 (0,0) fuera del clip");
    require_pixel(store, 199, 199, {0, 0, 0, 0}, "K5 (199,199) fuera del clip");
  }
  /* K6: rect fraccionario (0.25, 0.5, 100, 100). 949001 = 120000
   * + 9801x1 + 400x2048 (pixel_count 10201: 9801 interior + 400 borde).
   * Nota: pixel_count no es un campo de s::Result; se fija aquí la
   * cuenta geométrica del rect (el bucle de cargo itera exactamente
   * ese rango: ceil(r)-floor(l) por eje) y el presupuesto exacto fija
   * la partición interior/borde del kernel. */
  {
    auto r = s::render(store, prepare({gradient(0.25, 0.5, 100, 100, 1, 0, two_stops())}));
    require_budget(r, 949001, "K6 budget");
    const int pc = (int(std::ceil(100.25)) - int(std::floor(0.25))) *
                   (int(std::ceil(100.5)) - int(std::floor(0.5)));
    require(pc == 10201, "K6 pixel_count == 10201");
    require_pixel(store, 50, 50, {128, 0, 127, 255}, "K6 (50,50)");
  }
  /* K7: rect 10000x10000 en el origen, store 400x300. 240000 = 120000
   * (clear) + 120000 visibles x1 (t sobre los píxeles visibles
   * [0.00005, 0.03995], sin parada dentro del rango-t de ningún píxel). */
  {
    auto r = s::render(store, prepare({gradient(0, 0, 10000, 10000, 1, 0, two_stops())}));
    require_budget(r, 240000, "K7 budget");
    require_pixel(store, 0, 0, {0, 0, 255, 255}, "K7 (0,0)");
    require_pixel(store, 399, 299, {10, 0, 245, 255}, "K7 (399,299)");
  }
  std::printf("GRADIENT COST PASS (%zu checks)\n", checks);
  return 0;
}
