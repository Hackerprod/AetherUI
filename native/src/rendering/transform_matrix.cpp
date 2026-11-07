#include "transform_matrix.h"

#include <cmath>

namespace {

aether_transform_matrix identity() { return {1.f, 0.f, 0.f, 1.f, 0.f, 0.f}; }

/* Returns the operation produced by applying first and then second.  This is
   explicit about our row-vector convention and keeps CSS's right-to-left
   transform-list semantics out of renderer bindings. */
aether_transform_matrix compose(aether_transform_matrix first, aether_transform_matrix second) {
    return {
        first.m11 * second.m11 + first.m12 * second.m21,
        first.m11 * second.m12 + first.m12 * second.m22,
        first.m21 * second.m11 + first.m22 * second.m21,
        first.m21 * second.m12 + first.m22 * second.m22,
        first.dx * second.m11 + first.dy * second.m21 + second.dx,
        first.dx * second.m12 + first.dy * second.m22 + second.dy,
    };
}

float resolve_length(aether_css_length value, float reference, float font_size, float root_font_size,
                     aether_sizef viewport) {
    return aether_css_length_resolve(value, reference, font_size, root_font_size,
                                     viewport.width, viewport.height);
}

aether_transform_matrix operation_matrix(const aether_transform_operation& operation,
                                          aether_rectf border_box, float font_size,
                                          float root_font_size, aether_sizef viewport) {
    switch (operation.kind) {
        case AETHER_TRANSFORM_TRANSLATE:
            return {1.f, 0.f, 0.f, 1.f,
                    resolve_length(operation.x, border_box.width, font_size, root_font_size, viewport),
                    resolve_length(operation.y, border_box.height, font_size, root_font_size, viewport)};
        case AETHER_TRANSFORM_SCALE:
            return {operation.scalar_x, 0.f, 0.f, operation.scalar_y, 0.f, 0.f};
        case AETHER_TRANSFORM_ROTATE: {
            const float cosine = std::cos(operation.angle_radians);
            const float sine = std::sin(operation.angle_radians);
            return {cosine, sine, -sine, cosine, 0.f, 0.f};
        }
        case AETHER_TRANSFORM_SKEW_X:
            return {1.f, 0.f, std::tan(operation.angle_radians), 1.f, 0.f, 0.f};
        case AETHER_TRANSFORM_SKEW_Y:
            return {1.f, std::tan(operation.angle_radians), 0.f, 1.f, 0.f, 0.f};
        case AETHER_TRANSFORM_MATRIX:
            return {operation.matrix_a, operation.matrix_b, operation.matrix_c,
                    operation.matrix_d, operation.matrix_e, operation.matrix_f};
        default:
            return identity();
    }
}

} // namespace

aether_transform_matrix aether_transform_matrix_resolve(
    aether::rendering::IndexedReadRange<aether_transform_operation> transforms,
    aether_transform_origin origin,
    aether_rectf border_box,
    float font_size,
    float root_font_size,
    aether_sizef viewport) {
    aether_transform_matrix list_matrix = identity();
    if (!transforms.empty()) {
        // CSS applies the rightmost function first. With row vectors that is
        // the product of the operations visited in reverse declaration order.
        for (size_t index = transforms.size(); index-- > 0;) {
            list_matrix = compose(list_matrix,
                operation_matrix(transforms[index], border_box, font_size, root_font_size, viewport));
        }
    }
    const float origin_x = border_box.x + resolve_length(origin.x, border_box.width, font_size, root_font_size, viewport);
    const float origin_y = border_box.y + resolve_length(origin.y, border_box.height, font_size, root_font_size, viewport);
    return compose(compose({1.f, 0.f, 0.f, 1.f, -origin_x, -origin_y}, list_matrix),
                   {1.f, 0.f, 0.f, 1.f, origin_x, origin_y});
}

aether_pointf aether_transform_matrix_map_point(aether_transform_matrix matrix, aether_pointf point) {
    return {
        point.x * matrix.m11 + point.y * matrix.m21 + matrix.dx,
        point.x * matrix.m12 + point.y * matrix.m22 + matrix.dy,
    };
}

aether_transform_matrix aether_transform_matrix_resolve(
    const aether_transform_list& transforms, aether_transform_origin origin,
    aether_rectf border_box, float font_size, float root_font_size, aether_sizef viewport) {
    std::span<const aether_transform_operation> input;
    if (transforms.items && transforms.count > 0)
        input = {transforms.items, static_cast<size_t>(transforms.count)};
    return aether_transform_matrix_resolve(
        aether::rendering::IndexedReadRange<aether_transform_operation>(input),
        origin, border_box, font_size, root_font_size, viewport);
}
