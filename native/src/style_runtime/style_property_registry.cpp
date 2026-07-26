#include "style_property_registry_internal.h"

// ═══ Global singleton ══════════════════════════════════════════

static style_property_registry_s* g_global_registry = nullptr;

extern void builtin_properties_register(style_property_registry_s* registry);

style_property_registry_s* style_property_registry_get_global() {
    return g_global_registry;
}

void style_property_registry_init() {
    if (g_global_registry) return;
    g_global_registry = new style_property_registry_s();
    builtin_properties_register(g_global_registry);
    g_global_registry->seal();
}

void style_property_registry_shutdown() {
    delete g_global_registry;
    g_global_registry = nullptr;
}
