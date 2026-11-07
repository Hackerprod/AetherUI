#include "../src/rendering/style_kernel_readers.h"
#include "../src/rendering/transform_matrix.h"
#include "../src/rendering/border_painter.h"
#include "../src/rendering/stacking.h"
#include "../src/css/linear_gradient.h"
#include <cstdlib>
#include "snapshot_test_support.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>

using namespace aether::style;
using namespace aether::rendering;
template<class T> concept HasData = requires(T value) { value.data(); };
static_assert(!HasData<IndexedReadRange<aether_gradient_stop>>);
static_assert(std::is_same_v<decltype(std::declval<IndexedReadRange<aether_gradient_stop>>()[0]), const aether_gradient_stop&>);

static void need(bool condition) {
    if (!condition) { std::fputs("FAIL frozen paint kernel reader\n", stderr); std::exit(1); }
}

static void matrix_equal(aether_transform_matrix a, aether_transform_matrix b) {
    need(a.m11 == b.m11 && a.m12 == b.m12 && a.m21 == b.m21 && a.m22 == b.m22 && a.dx == b.dx && a.dy == b.dy);
}
int main() {
    aether_transform_operation operations[4]{};
    operations[0].kind = AETHER_TRANSFORM_TRANSLATE;
    operations[0].x = {15, AETHER_CSS_UNIT_PERCENT};
    operations[0].y = {3, AETHER_CSS_UNIT_EM};
    operations[1].kind = AETHER_TRANSFORM_SCALE; operations[1].scalar_x = 2; operations[1].scalar_y = .75f;
    operations[2].kind = AETHER_TRANSFORM_ROTATE; operations[2].angle_radians = .3f;
    operations[3].kind = AETHER_TRANSFORM_SKEW_X; operations[3].angle_radians = -.2f;
    auto frozen_ops = ImmutableArray<aether_transform_operation>::generate(4, [&](size_t i) { return operations[i]; });
    const aether_transform_origin origin{{25, AETHER_CSS_UNIT_PERCENT}, {7, AETHER_CSS_UNIT_PX}};
    const aether_rectf box{11, 17, 250, 70};
    const aether_sizef viewport{700, 500};
    auto flat_matrix = aether_transform_matrix_resolve(aether_transform_list{operations, 4}, origin, box, 13, 17, viewport);
    matrix_equal(flat_matrix, aether_transform_matrix_resolve(IndexedReadRange<aether_transform_operation>(frozen_ops.view()), origin, box, 13, 17, viewport));
    operations[0].x.value = 999;
    matrix_equal(flat_matrix, aether_transform_matrix_resolve(IndexedReadRange<aether_transform_operation>(frozen_ops.view()), origin, box, 13, 17, viewport));
    const auto empty = aether_transform_matrix_resolve(IndexedReadRange<aether_transform_operation>{}, origin, box, 13, 17, viewport);
    for (int count : {-1, 0, 2}) {
        matrix_equal(empty, aether_transform_matrix_resolve(aether_transform_list{nullptr, count}, origin, box, 13, 17, viewport));
    }
    matrix_equal(empty, aether_transform_matrix_resolve(aether_transform_list{operations, -1}, origin, box, 13, 17, viewport));

    aether_gradient_stop input[4]{};
    for (int i = 0; i < 4; ++i) { input[i].color = {float(i) / 4, .2f, .8f, 1}; input[i].offset = float(i) / 3; }
    input[0].has_position = AETHER_TRUE; input[0].position = {-10, AETHER_CSS_UNIT_PX};
    input[2].has_position = AETHER_TRUE; input[2].position = {60, AETHER_CSS_UNIT_PERCENT};
    input[3].has_position = AETHER_TRUE; input[3].position = {4, AETHER_CSS_UNIT_EM};
    auto frozen_stops = ImmutableArray<aether_gradient_stop>::generate(4, [&](size_t i) { return input[i]; });
    const aether_linear_gradient source{1, .25f, input, 4};
    aether_linear_gradient_resolution_context_s context{}; context.gradient_rect = box; context.font_size = 20;
    aether_linear_gradient flat{}, frozen{};
    need(aether_linear_gradient_resolve(source, context, &flat));
    need(aether_linear_gradient_resolve(GradientKernelReadView(GradientReadView{1, .25f, frozen_stops.view()}), context, &frozen));
    need(flat.direction_x == frozen.direction_x && flat.direction_y == frozen.direction_y && flat.stop_count == frozen.stop_count);
    for (int i = 0; i < flat.stop_count; ++i) {
        need(flat.stops[i].offset == frozen.stops[i].offset);
        need(flat.stops[i].color.r == frozen.stops[i].color.r);
        need(flat.stops[i].has_position == frozen.stops[i].has_position);
        need(flat.stops[i].position.value == frozen.stops[i].position.value);
    }
    aether_linear_gradient_free(&flat); aether_linear_gradient_free(&frozen);
    for (int count : {-1, 0, 2}) {
        aether_linear_gradient sentinel{9, 8, nullptr, 7};
        need(!aether_linear_gradient_resolve(aether_linear_gradient{1, 0, nullptr, count}, context, &sentinel));
        need(sentinel.direction_x == 9 && sentinel.stop_count == 7);
    }
    aether_linear_gradient sentinel{9, 8, nullptr, 7};
    auto negative = context; negative.gradient_rect.width = -1;
    need(!aether_linear_gradient_resolve(source, negative, &sentinel)); need(sentinel.direction_x == 9);
    auto zero = context; zero.gradient_rect = {};
    need(!aether_linear_gradient_resolve(source, zero, &sentinel)); need(sentinel.direction_x == 0 && sentinel.stop_count == 0);

    aether_computed_style_t style{};
    style.opacity = 1; style.position = AETHER_POSITION_RELATIVE;
    style.z_index_kind = AETHER_Z_INDEX_INTEGER; style.z_index = -3;
    style.font_size = 16; style.border_styles = {AETHER_BORDER_STYLE_SOLID, AETHER_BORDER_STYLE_DOUBLE, AETHER_BORDER_STYLE_DASHED, AETHER_BORDER_STYLE_DOTTED};
    style.border_colors = {{1,0,0,1}, {0,1,0,1}, {0,0,1,1}, {1,1,0,1}};
    const auto scalars = StyleScalars::copy_from(style);
    auto a = aether::stacking::classify_element_paint(style);
    auto b = aether::stacking::classify_element_paint(scalars);
    need(a.cls == b.cls && a.z_value == b.z_value && a.has_explicit_z_index == b.has_explicit_z_index);
    need(aether::stacking::creates_stacking_context(style) == aether::stacking::creates_stacking_context(scalars, false, false));
    style.position = AETHER_POSITION_STATIC; style.z_index_kind = AETHER_Z_INDEX_AUTO;
    style.transforms = {nullptr, 2};
    need(aether::stacking::creates_stacking_context(style)); // Historical count-only check.
    std::vector<aether_paint_command_t> commands_a, commands_b;
    aether_paint_border(scalars, box, {}, aether_thicknessf{3,4,5,6}, commands_a);
    scalars.copy_to(style);
    aether_paint_border(style, box, {}, aether_thicknessf{3,4,5,6}, commands_b);
    need(commands_a.size() == commands_b.size());
    for (size_t i = 0; i < commands_a.size(); ++i) {
        need(commands_a[i].tag == AETHER_PAINT_FILL_BORDER_SIDE && commands_b[i].tag == commands_a[i].tag);
        const auto& x = commands_a[i].data.fill_border_side;
        const auto& y = commands_b[i].data.fill_border_side;
        snapshot_test::Digest dx, dy;
        dx.rect(x.outer_rect); dx.radii(x.outer_radii); dx.rect(x.inner_rect); dx.radii(x.inner_radii);
        dy.rect(y.outer_rect); dy.radii(y.outer_radii); dy.rect(y.inner_rect); dy.radii(y.inner_radii);
        dx.scalar(x.side); dx.scalar(x.style); dx.color(x.color);
        dy.scalar(y.side); dy.scalar(y.style); dy.color(y.color);
        need(dx.hash == dy.hash);
        aether_paint_command_free(&commands_a[i]); aether_paint_command_free(&commands_b[i]);
    }
    std::puts("frozen paint kernel readers: PASS");
}
