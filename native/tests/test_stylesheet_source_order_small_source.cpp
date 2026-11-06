// Private test translation unit: inject a small byte limit into the exact
// compiler rejection path, without a production switch or an installed hook.
#include "../src/style_runtime/style_source_limits.h"
static bool small_source_size_fits(size_t size) noexcept {
    return style_source_size_fits(size) && size <= 128;
}
#define style_source_size_fits small_source_size_fits
#include "../src/style_runtime/style_source_compiler.cpp"
#undef style_source_size_fits
