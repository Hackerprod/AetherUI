#include "style_map_version.h"
#include "style_revision.h"
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
    if (!in.backing && (!in.revision || in.mode!=MapMode::contextual || in.revision->backing())) return AETHER_ERROR_INVALID_STATE;
    if (in.mode==MapMode::legacy_partial) {
        if (in.revision || in.generation) return AETHER_ERROR_INVALID_STATE;
    } else if (in.mode==MapMode::contextual || in.mode==MapMode::legacy_complete) {
        if (!in.revision || !in.document_lifetime || !in.generation) return AETHER_ERROR_INVALID_STATE;
        const auto& r = *in.revision;
        if (in.backing != r.backing() || in.backing.owner_before(r.backing()) || r.backing().owner_before(in.backing) ||
            in.document_lifetime != r.document_lifetime() ||
            in.document_lifetime.owner_before(r.document_lifetime()) || r.document_lifetime().owner_before(in.document_lifetime) ||
            in.document_identity != r.document_identity() || in.generation != r.generation() ||
            in.input_epoch != r.input_epoch() || in.structure_epoch != r.structure_epoch() ||
            in.style_tree_epoch != r.style_tree_epoch() || (in.mode == MapMode::contextual && !r.contextual()))
            return AETHER_ERROR_INVALID_STATE;
    } else return AETHER_ERROR_INVALID_STATE;
    if (in.document_lifetime) {
        if (!in.document_lifetime->owner ||
            (in.document_lifetime->alive && static_cast<const aether_node_s*>(in.document_lifetime->owner)->identity != in.document_lifetime) || in.document_identity != reinterpret_cast<uintptr_t>(in.document_lifetime.get())) return AETHER_ERROR_INVALID_STATE;
    } else if (in.document_identity) return AETHER_ERROR_INVALID_STATE;
    try { MapVersionRef result(new const MapVersion(in)); out=std::move(result); return AETHER_OK; }
    catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
}
aether_status_t materialize_legacy_revision_map(const StyleRevisionRef& revision,MapVersionRef& out) noexcept {
    if(!revision)return AETHER_ERROR_INVALID_STATE;
    try {
        MapVersionInput input;input.mode=MapMode::legacy_complete;input.revision=revision;
        input.document_lifetime=revision->document_lifetime();input.document_identity=revision->document_identity();
        input.generation=revision->generation();input.structure_epoch=revision->structure_epoch();
        input.input_epoch=revision->input_epoch();input.style_tree_epoch=revision->style_tree_epoch();
        if(revision->backing()){input.backing=revision->backing();return make_map_version(input,out);}
        BackingBuildInput backing;backing.entries.reserve(revision->directory().size());
        for(const auto& entry:revision->directory().entries()) {
            const auto view=BoundNodeView::find(revision,entry.second.payload->node());FrozenNodeRef record;
            const auto status=view.export_record(record);if(status!=AETHER_OK)return status;
            backing.entries.emplace(record->node(),std::move(record));
        }
        const auto status=freeze_backing(backing,input.backing);if(status!=AETHER_OK)return status;
        // Only this factory can attach a materialized backing to a payload-only
        // revision. Each leaf was exported from that exact authentic revision.
        MapVersionRef result(new const MapVersion(input));out=std::move(result);return AETHER_OK;
    }catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}
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
