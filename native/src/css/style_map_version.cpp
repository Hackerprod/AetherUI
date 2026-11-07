#include "style_map_version.h"
#include "../dom/dom_types.h"
namespace aether::style {
MapVersion::MapVersion(const MapVersionInput& in) :
    backing_(in.backing),
    mode_(in.mode),
    revision_(in.revision),
    document_lifetime_(in.document_lifetime),
    document_identity_(in.document_identity),
    generation_(in.generation),
    structure_epoch_(in.structure_epoch),
    input_epoch_(in.input_epoch),
    style_tree_epoch_(in.style_tree_epoch) {}
aether_status_t make_map_version(const MapVersionInput& in, MapVersionRef& out) noexcept {
    if (!in.backing) return AETHER_ERROR_INVALID_STATE;
    if (in.mode==MapMode::legacy_partial) {
        if (in.revision || in.generation) return AETHER_ERROR_INVALID_STATE;
    } else if (in.mode==MapMode::contextual || in.mode==MapMode::legacy_complete) {
        if (!in.revision || !in.document_lifetime || !in.generation) return AETHER_ERROR_INVALID_STATE;
    } else return AETHER_ERROR_INVALID_STATE;
    if (in.document_lifetime) {
        if (!in.document_lifetime->owner ||
            (in.document_lifetime->alive && static_cast<const aether_node_s*>(in.document_lifetime->owner)->identity != in.document_lifetime) || in.document_identity != reinterpret_cast<uintptr_t>(in.document_lifetime.get())) return AETHER_ERROR_INVALID_STATE;
    } else if (in.document_identity) return AETHER_ERROR_INVALID_STATE;
    try { MapVersionRef result(new const MapVersion(in)); out=std::move(result); return AETHER_OK; }
    catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
}
bool structure_valid(const MapVersion* v) noexcept {
    return !v || !v->document_lifetime() || (v->document_lifetime()->alive && v->document_lifetime()->structure_epoch==v->structure_epoch());
}
bool inputs_current(const MapVersion* v) noexcept {
    return !v || !v->contextual() || !v->document_lifetime() || (v->document_lifetime()->alive &&
        v->document_lifetime()->mutation_epoch==v->input_epoch() && v->document_lifetime()->style_tree_epoch==v->style_tree_epoch());
}
bool accepts_override(const MapVersion* base,const MapVersion* over) noexcept {
    return !base || !over || !base->contextual() || (over->contextual() && base->revision()==over->revision() &&
        base->document_identity()==over->document_identity() && base->generation()==over->generation());
}
}
