#pragma once
// Private partial comparison only. This type is deliberately unrelated to
// SyntheticFixtureAuthority/SyntheticFaceSeal and cannot authorize shaping.
#include "../css/style_map_version.h"
#include "../css/style_revision.h"
#include <utility>
namespace aether::style {
// Historical valid() is ownership consistency only. For operations, check
// current runtime/document/thread/epochs before and after callbacks. No authority.
bool typography_inputs_current(aether_style_runtime_t, const MapVersionRef&) noexcept;
enum class TypographyComparison { different, equal_for_tracked_fields, unproven };
class TypographyContext final {
    const MapVersionRef version_;
    const FrozenNodeRef record_;
    TypographyContext(MapVersionRef version, FrozenNodeRef record) noexcept
        : version_(std::move(version)), record_(std::move(record)) {}
public:
    // No independently supplied evidence or scalars: the exact frozen record
    // must be owned by this contextual revision. A mismatch creates an invalid
    // descriptor, not a repaired/reconstructed context.
    static TypographyContext bind(MapVersionRef version, FrozenNodeRef record) noexcept {
        return TypographyContext(std::move(version), std::move(record));
    }
    bool valid() const noexcept {
        if (!version_ || !record_ || !version_->contextual() || !version_->revision() ||
            !version_->backing() || !record_->typography() || !record_->style() ||
            record_->origin()!=RecordOrigin::runtime ||
            version_->backing()->find(record_->node())!=record_.get()) return false;
        const auto owned=version_->backing()->find_owner(record_->node());
        if (owned.owner_before(record_) || record_.owner_before(owned)) return false;
        const auto& revision=*version_->revision();
        const auto& t=*record_->typography();
        return revision.contextual() && revision.backing()==version_->backing() &&
            record_->document_identity()==version_->document_identity() &&
            record_->document_lifetime()==version_->document_lifetime() &&
            revision.document_lifetime()==version_->document_lifetime() &&
            t.document_identity==version_->document_identity() &&
            t.document_identity==revision.document_identity() &&
            t.generation==version_->generation() && t.generation==revision.generation() &&
            t.input_epoch==version_->input_epoch() && t.input_epoch==revision.input_epoch() &&
            t.structure_epoch==version_->structure_epoch() && t.structure_epoch==revision.structure_epoch() &&
            t.style_tree_epoch==version_->style_tree_epoch() && t.style_tree_epoch==revision.style_tree_epoch() &&
            t.sheet_epoch==revision.sheet_epoch() && t.registry_version==1;
    }
    const TypographyEvidence* evidence() const noexcept {
        return valid()?&record_->typography()->evidence:nullptr;
    }
    friend TypographyComparison compare_typography_context(const TypographyContext&, const TypographyContext&) noexcept;
};
inline TypographyComparison compare_typography_context(const TypographyContext& a, const TypographyContext& b) noexcept {
    // Identity first, even when an apparent known difference exists. Distinct
    // wrappers cloning the same immutable revision are permitted; revisions are not.
    if (!a.valid() || !b.valid() || a.version_->revision()!=b.version_->revision())
        return TypographyComparison::unproven;
    const auto& x=a.record_->typography()->evidence;
    const auto& y=b.record_->typography()->evidence;
    constexpr auto known=TypographyFieldState::known;
    bool incomplete=false, different=false;
    auto field=[&](TypographyFieldState sx, TypographyFieldState sy, bool equal) {
        if(sx==known && sy==known) different=different || !equal;
        else incomplete=true;
    };
    field(x.family_state,y.family_state,x.families==y.families);
    field(x.style_state,y.style_state,x.font_style==y.font_style);
    field(x.spacing_state,y.spacing_state,x.spacing_normal==y.spacing_normal && x.spacing_px==y.spacing_px);
    field(x.size_state,y.size_state,x.size_px==y.size_px && x.size_dependencies==y.size_dependencies);
    field(x.weight_state,y.weight_state,x.weight==y.weight);
    field(x.line_height_state,y.line_height_state,x.line_height_kind==y.line_height_kind &&
        x.line_height_value==y.line_height_value && x.line_height_used_px==y.line_height_used_px &&
        x.line_height_dependencies==y.line_height_dependencies);
    // A known difference is useful even if another field is unknown. Equal
    // unknowns are never proof. Equality says nothing about untracked font face,
    // features, shaping language/script, bidi, writing mode or grouping barriers.
    return different?TypographyComparison::different:incomplete?TypographyComparison::unproven:
        TypographyComparison::equal_for_tracked_fields;
}
}
