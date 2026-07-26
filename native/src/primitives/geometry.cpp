#include <aether/aether.h>
#include <algorithm>

AETHER_API aether_bool_t aether_rectf_contains(aether_rectf r, aether_pointf p) {
    return (p.x >= r.x && p.x <= r.x + r.width &&
            p.y >= r.y && p.y <= r.y + r.height) ? AETHER_TRUE : AETHER_FALSE;
}

AETHER_API aether_rectf aether_rectf_deflate(aether_rectf r, aether_thicknessf t) {
    aether_rectf out;
    out.x = r.x + t.left;
    out.y = r.y + t.top;
    out.width = std::max(0.0f, r.width - (t.left + t.right));
    out.height = std::max(0.0f, r.height - (t.top + t.bottom));
    return out;
}

AETHER_API aether_rectf aether_rectf_inflate(aether_rectf r, float v) {
    aether_rectf out;
    out.x = r.x - v;
    out.y = r.y - v;
    out.width = r.width + v * 2;
    out.height = r.height + v * 2;
    return out;
}

AETHER_API aether_rectf aether_rectf_offset(aether_rectf r, float dx, float dy) {
    aether_rectf out;
    out.x = r.x + dx;
    out.y = r.y + dy;
    out.width = r.width;
    out.height = r.height;
    return out;
}

AETHER_API aether_rectf aether_rectf_from_edges(float left, float top, float right, float bottom) {
    aether_rectf out;
    out.x = left;
    out.y = top;
    out.width = std::max(0.0f, right - left);
    out.height = std::max(0.0f, bottom - top);
    return out;
}

AETHER_API float aether_thicknessf_horizontal(aether_thicknessf t) {
    return t.left + t.right;
}

AETHER_API float aether_thicknessf_vertical(aether_thicknessf t) {
    return t.top + t.bottom;
}
