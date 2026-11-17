#pragma once
#include "typography_evidence.h"
#include "style_source_accounting.h"
#include "../rendering/text/native_paragraph_diagnostic.h"
#include <cstdint>
namespace aether::style {
// Owned by the immutable revision's node record. Never authorizes rendering.
struct TypographyRecord {
    // Authentic post-discard/computed, pre-final-accounting evidence.
    // Owned by this immutable record, independently of optional diagnostics.
    TypographyEvidenceRef prepared_evidence;
    TypographyEvidence evidence;
    std::shared_ptr<const style_source_accounting_s> inputs;
    aether::rendering::text::ParagraphDiagnosticRef text_diagnostic;
    // Independent prerequisites, not alternatives. Neither is established here.
    const bool context_unproven=true, font_unresolved=true;
    uint64_t generation=0, document_identity=0, input_epoch=0, structure_epoch=0,
        style_tree_epoch=0, sheet_epoch=0, registry_version=1;
};
// Same-stage production comparators. Missing proof is conservative. Counters
// are deliberately absent here, not from publication or budget enforcement.
inline bool prepared_typography_changed(const TypographyRecord* previous,
                                        const TypographyEvidence& next) {
    return !previous || !previous->prepared_evidence ||
        *previous->prepared_evidence != next;
}
inline bool final_typography_changed(const TypographyRecord* previous,
                                     const TypographyEvidence& next,
                                     const style_source_accounting_s& inputs) {
    return !previous || !previous->inputs || previous->evidence != next ||
        previous->inputs->losses != inputs.losses ||
        previous->inputs->inputs_accounted != inputs.inputs_accounted ||
        previous->inputs->external_pending != inputs.external_pending ||
        !(previous->inputs->attribution == inputs.attribution);
}
// Additional visual contribution, not evidence equality. The base semantic
// classifier already handles known line-height kind/value/dependency changes.
// Do not infer this from an impact mask or equal used pixels.
enum class TypographyVisualDelta { none, line_height_only, computed_noop, conservative };
inline bool classified_noop_fields(const TypographyEvidence& e) {
    const bool style=e.noop_absence_valid(0) ||
        (e.noop_absence[0]==NoopAbsenceProof::unproven && e.style_state==TypographyFieldState::known && e.provenance[1].declaration &&
         !e.provenance[1].accepted_source.empty() &&
         (e.font_style=="normal" || e.font_style=="italic" || e.font_style=="oblique"));
    const bool spacing=e.noop_absence_valid(1) ||
        (e.noop_absence[1]==NoopAbsenceProof::unproven && e.spacing_state==TypographyFieldState::known && e.provenance[2].declaration &&
         !e.provenance[2].accepted_source.empty() && std::isfinite(e.spacing_px));
    return style && spacing;
}
inline bool computed_noop_delta(const TypographyEvidence& before, const TypographyEvidence& after,
                                bool ignore_accounting_gap=false) {
    if (!before.same_core_fields(after,ignore_accounting_gap) || !before.same_line_height_fields(after)) return false;
    if (before.same_noop_fields(after)) return true;
    // Unknown-to-unknown must not launder an inherited/proof-only transition.
    if (before.style_state==TypographyFieldState::unknown && after.style_state==TypographyFieldState::unknown &&
        (!(before.provenance[1]==after.provenance[1]) || before.noop_absence[0]!=after.noop_absence[0])) return false;
    if (before.spacing_state==TypographyFieldState::unknown && after.spacing_state==TypographyFieldState::unknown &&
        (!(before.provenance[2]==after.provenance[2]) || before.noop_absence[1]!=after.noop_absence[1])) return false;
    return classified_noop_fields(before) && classified_noop_fields(after);
}
inline bool classified_line_height(const TypographyEvidence& evidence) {
    if (evidence.line_height_state != TypographyFieldState::known ||
        !std::isfinite(evidence.line_height_value) || !std::isfinite(evidence.line_height_used_px)) return false;
    switch (evidence.line_height_kind) {
    case TypographyLineHeightKind::normal:
    case TypographyLineHeightKind::number:
    case TypographyLineHeightKind::absolute_css_px: return true;
    }
    return false;
}
inline TypographyVisualDelta typography_visual_delta(const TypographyEvidence& before,
                                                       const TypographyEvidence& after) {
    if (before == after) return TypographyVisualDelta::none;
    if (classified_line_height(before) && classified_line_height(after) &&
        before.same_non_line_height_fields(after))
        return TypographyVisualDelta::line_height_only;
    if (computed_noop_delta(before,after)) return TypographyVisualDelta::computed_noop;
    return TypographyVisualDelta::conservative;
}
inline TypographyVisualDelta prepared_typography_visual_delta(const TypographyRecord* previous,
                                                               const TypographyEvidence& next) {
    if (!previous || !previous->prepared_evidence) return TypographyVisualDelta::conservative;
    return typography_visual_delta(*previous->prepared_evidence, next);
}
inline TypographyVisualDelta final_typography_visual_delta(const TypographyRecord* previous,
                                                            const TypographyEvidence& next,
                                                            const style_source_accounting_s& inputs) {
    if (!previous || !previous->inputs) return TypographyVisualDelta::conservative;
    // Identical conservative attribution is a no-op, not an eternal fallback.
    if (!final_typography_changed(previous,next,inputs)) return TypographyVisualDelta::none;
    const auto& before=*previous->inputs;
    if (before.inputs_accounted != inputs.inputs_accounted || before.external_pending != inputs.external_pending ||
        before.attribution.complete != inputs.attribution.complete ||
        before.attribution_consistent() != inputs.attribution_consistent())
        return TypographyVisualDelta::conservative;
    const bool proof_changed=!(before.attribution==inputs.attribution);
    const bool losses_changed=before.losses!=inputs.losses;
    const bool gap_changed=((previous->evidence.coverage_gaps ^ next.coverage_gaps)&4u)!=0;
    if (proof_changed || losses_changed || gap_changed) {
        // Gap4 may only be ignored when it agrees with the certified accounting
        // on BOTH sides. Counter changes are still detected by strict equality.
        const auto authentic_gap=[](const TypographyEvidence& e,const style_source_accounting_s& a) {
            return bool(e.coverage_gaps&4u)==bool(a.losses || !a.inputs_accounted);
        };
        if (before.visual_losses_certified() && inputs.visual_losses_certified() &&
            before.inputs_accounted && !before.external_pending &&
            authentic_gap(previous->evidence,before) && authentic_gap(next,inputs) &&
            computed_noop_delta(previous->evidence,next,true)) return TypographyVisualDelta::computed_noop;
        return TypographyVisualDelta::conservative;
    }
    const auto delta=typography_visual_delta(previous->evidence,next);
    if (delta==TypographyVisualDelta::computed_noop &&
        (!before.visual_losses_certified() || !inputs.visual_losses_certified() ||
         !before.inputs_accounted || before.external_pending)) return TypographyVisualDelta::conservative;
    return delta;
}
using TypographyRecordRef=std::shared_ptr<const TypographyRecord>;
}
