#include "style_property_registry_internal.h"

// ═══ Global singleton ══════════════════════════════════════════

static std::shared_ptr<style_property_registry_s> g_global_registry;

extern void builtin_properties_register(style_property_registry_s* registry);

style_property_registry_s* style_property_registry_get_global() {
    return g_global_registry.get();
}

void style_property_registry_init() {
    if (g_global_registry) return;
    auto candidate=std::make_shared<style_property_registry_s>();
    builtin_properties_register(candidate.get());
    candidate->seal();
    g_global_registry=std::move(candidate);
}

void style_property_registry_shutdown() {
    g_global_registry.reset();
}

std::shared_ptr<const style_property_registry_s> style_property_registry_pin() {
    return g_global_registry;
}
