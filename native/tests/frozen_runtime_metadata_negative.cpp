// Compile separately with one FROZEN_METADATA_NEGATIVE selector. Every selected
// write must fail at compile time; this file is not a runtime test target.
#include "../src/style_runtime/frozen_runtime_metadata.h"
#include "../src/css/frozen_measure_read.h"
using namespace aether::style;
void forbidden(FrozenScopeRef scope, FrozenColorRef colors, FrozenMeasuresRef measures) {
#if FROZEN_METADATA_NEGATIVE == 1
    scope->find_local("--x")->value()[0] = 'x';
#elif FROZEN_METADATA_NEGATIVE == 2
    scope->find_local("--x")->status() = custom_property_status::valid;
#elif FROZEN_METADATA_NEGATIVE == 3
    colors->border(0) = false;
#elif FROZEN_METADATA_NEGATIVE == 4
    colors->text_shadow_at(0) = false;
#elif FROZEN_METADATA_NEGATIVE == 5
    measures->scalars().self_font_size = 7;
#elif FROZEN_METADATA_NEGATIVE == 6
    measures->document().values().root_font_size = 7;
#elif FROZEN_METADATA_NEGATIVE == 7
    scope->parent() = nullptr;
#elif FROZEN_METADATA_NEGATIVE == 8
    measures->background_positions()[0].x.value = 7;
#else
#error A negative selector is required
#endif
}
