#pragma once

// Private composition entry points. The caller owns an unsealed registry,
// serializes registration, and seals it after composing every property family.
// Longhands and shorthands are separate to preserve catalog order and IDs.
struct style_property_registry_s;
void transition_properties_register_longhands(style_property_registry_s* registry);
void transition_properties_register_shorthands(style_property_registry_s* registry);
