#pragma once
#include "typography_record.h"
#include "../css/frozen_measure_read.h"
#include <string_view>
namespace aether::style {
// Borrowed values from the SAME pair used by classify_style_diff/publication.
// Not a certificate: callers must establish record/node identity first.
struct TypographyProjection {
    const char* family;
    float size;
    int weight;
    measure_line_height_s lh;
    float used;
    uint32_t size_deps, lh_deps;
    float self_font_size=std::numeric_limits<float>::quiet_NaN();
};
inline TypographyProjection typography_projection(const StyleScalars& s, const FrozenMeasures& m) {
    const MeasuresReadView view(m);
    return {s.font_family.data(),s.font_size,s.font_weight,view.line_height(),
        measure_used_line_height(&view,s),m.dependency("font-size"),m.dependency("line-height"),view.self_font_size()};
}
inline TypographyProjection typography_projection(const aether_computed_style_t& s, const measure_state_s& m) {
    return {s.font_family,s.font_size,s.font_weight,m.line_height,measure_used_line_height(&m,s),
        measure_property_dependencies(m,"font-size"),measure_property_dependencies(m,"line-height"),m.self_font_size};
}
inline TypographyProjection typography_projection(const StyleScalars& s, const measure_state_s& m) {
    const MeasuresReadView view(m);
    return {s.font_family.data(),s.font_size,s.font_weight,m.line_height,measure_used_line_height(&view,s),
        measure_property_dependencies(m,"font-size"),measure_property_dependencies(m,"line-height"),m.self_font_size};
}
inline bool typography_controls(const TypographyProvenance& a,const TypographyProvenance& b) {
    return a.declaration && b.declaration && a.inherited==b.inherited && a.important==b.important &&
        a.inline_style==b.inline_style && a.layer==b.layer && a.source_order==b.source_order &&
        a.declaration_order==b.declaration_order && !a.accepted_source.empty() && !b.accepted_source.empty() &&
        a.accepted_source.size()<=65536 && b.accepted_source.size()<=65536;
}
inline bool typography_sources(const TypographyProvenance& a,const TypographyProvenance& b) {
    return typography_controls(a,b) && !a.resolved_source.empty() && !b.resolved_source.empty() &&
        a.resolved_source.size()<=65536 && b.resolved_source.size()<=65536;
}
inline bool projected_family(const TypographyEvidence& e,const TypographyProjection& p) {
    if(e.family_state!=TypographyFieldState::known || e.families.size()!=1 || !p.family ||
        !e.provenance[0].resolved_source.empty()) return false;
    const auto& f=e.families[0]; const auto& source=e.provenance[0].accepted_source;
    if(f.name.empty() || f.name.size()>63 || source!=f.name) return false;
    const auto letter=[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z');};
    if(!letter(source[0])) return false;
    for(char c:source) if(!letter(c) && !(c>='0'&&c<='9') && c!='-') return false;
    const auto lower=typography_ascii_lower(source);
    if(typography_css_wide(lower) || lower=="default" || f.generic!=typography_generic(lower) ||
        (f.generic && source!=lower)) return false;
    size_t n=0; while(n<64 && p.family[n]) ++n;
    return n<64 && std::string_view(p.family,n)==f.name;
}
inline bool projected_size(const TypographyEvidence& e,const TypographyProjection& p) {
    auto deps=p.size_deps; const auto source=typography_ascii_lower(e.provenance[3].resolved_source);
    if(source=="smaller" || source=="larger") deps|=MEASURE_PARENT;
    return e.size_state==TypographyFieldState::known && std::isfinite(e.size_px) && e.size_px>0 &&
        e.size_px==p.size && e.size_dependencies==deps;
}
inline bool projected_weight(const TypographyEvidence& e,const TypographyProjection& p) {
    return e.weight_state==TypographyFieldState::known && e.weight>=100 && e.weight<=900 && e.weight==p.weight;
}
inline bool projected_lh(const TypographyEvidence& e,const TypographyProjection& p) {
    TypographyLineHeightKind kind;
    switch(p.lh.kind) {
    case measure_line_height_kind_e::normal: kind=TypographyLineHeightKind::normal;break;
    case measure_line_height_kind_e::number: kind=TypographyLineHeightKind::number;break;
    case measure_line_height_kind_e::absolute_css_px: kind=TypographyLineHeightKind::absolute_css_px;break;
    default: return false;
    }
    return std::isfinite(p.size) && p.self_font_size==p.size && classified_line_height(e) && e.line_height_kind==kind &&
        e.line_height_value==p.lh.value && e.line_height_used_px==p.used &&
        e.line_height_dependencies==(p.lh_deps | (p.lh.kind==measure_line_height_kind_e::absolute_css_px ? 0u : uint32_t(MEASURE_SELF_FONT)));
}
inline bool same_noop_slot(const TypographyEvidence& a,const TypographyEvidence& b,size_t slot) {
    return a.noop_absence[slot]==b.noop_absence[slot] && a.provenance[slot+1]==b.provenance[slot+1] &&
        (slot==0 ? a.style_state==b.style_state && a.font_style==b.font_style :
        a.spacing_state==b.spacing_state && a.spacing_normal==b.spacing_normal && a.spacing_px==b.spacing_px);
}
inline bool classified_noop_slot(const TypographyEvidence& e,size_t slot) {
    if(e.noop_absence_valid(slot)) return true;
    if(e.noop_absence[slot]!=NoopAbsenceProof::unproven || !e.provenance[slot+1].declaration ||
        e.provenance[slot+1].accepted_source.empty()) return false;
    return slot==0 ? e.style_state==TypographyFieldState::known &&
        (e.font_style=="normal" || e.font_style=="italic" || e.font_style=="oblique") :
        e.spacing_state==TypographyFieldState::known && std::isfinite(e.spacing_px);
}
struct TypographyContribution {
    bool residual=false, noop_changed=false;
    void add(bool uncertain,bool noop=false) { residual|=uncertain; noop_changed|=noop; }
    uint32_t impact() const { return residual ? 19u : 0u; }
};
enum class LHProjectionGuard { stable, changed, unproven };
inline LHProjectionGuard lh_projection_guard(const TypographyProjection* old,const TypographyProjection* next) {
    if(!old || !next) return LHProjectionGuard::unproven;
    return old->lh_deps==next->lh_deps ? LHProjectionGuard::stable : LHProjectionGuard::changed;
}
inline TypographyContribution compose_typography(const TypographyEvidence& a,const TypographyEvidence& b,
    const TypographyProjection& old,const TypographyProjection& next) {
    TypographyContribution result;
    result.add(lh_projection_guard(&old,&next)!=LHProjectionGuard::stable);
    if(!(a.family_state==b.family_state && a.families==b.families && a.provenance[0]==b.provenance[0]))
        result.add(!(typography_controls(a.provenance[0],b.provenance[0]) && projected_family(a,old) && projected_family(b,next)));
    if(!(a.size_state==b.size_state && a.size_px==b.size_px && a.size_dependencies==b.size_dependencies && a.provenance[3]==b.provenance[3]))
        result.add(!(typography_sources(a.provenance[3],b.provenance[3]) && a.size_dependencies==b.size_dependencies && projected_size(a,old) && projected_size(b,next)));
    if(!(a.weight_state==b.weight_state && a.weight==b.weight && a.provenance[4]==b.provenance[4]))
        result.add(!(typography_sources(a.provenance[4],b.provenance[4]) && projected_weight(a,old) && projected_weight(b,next)));
    if(!a.same_line_height_fields(b))
        result.add(!(typography_sources(a.provenance[5],b.provenance[5]) && old.lh_deps==next.lh_deps && projected_lh(a,old) && projected_lh(b,next)));
    for(size_t slot=0;slot<2;++slot) if(!same_noop_slot(a,b,slot)) {
        const auto as=slot==0?a.style_state:a.spacing_state, bs=slot==0?b.style_state:b.spacing_state;
        const bool unknown_transition=as==TypographyFieldState::unknown && bs==TypographyFieldState::unknown &&
            (!(a.provenance[slot+1]==b.provenance[slot+1]) || a.noop_absence[slot]!=b.noop_absence[slot]);
        result.add(unknown_transition || !classified_noop_slot(a,slot) || !classified_noop_slot(b,slot),true);
    }
    result.add(!(a.break_policy==b.break_policy) || a.coverage_complete!=b.coverage_complete || a.coverage_gaps!=b.coverage_gaps);
    return result;
}
inline uint32_t composed_prepared_impact(const TypographyRecord* previous,const TypographyEvidence& next,
    const TypographyProjection& old,const TypographyProjection& current) {
    if(!previous || !previous->prepared_evidence) return 19;
    return compose_typography(*previous->prepared_evidence,next,old,current).impact();
}
inline bool typography_final_relation(const TypographyEvidence& p,const TypographyEvidence& f) {
    return p.same_core_fields(f,true) && p.same_noop_fields(f) && p.same_line_height_fields(f) &&
        !(p.coverage_gaps&4u); // F is P with at most accounting gap4 added.
}
inline uint32_t composed_final_impact(const TypographyRecord* previous,const TypographyEvidence& prepared,
    const TypographyEvidence& final,const style_source_accounting_s& inputs,
    const TypographyProjection& old,const TypographyProjection& current) {
    // Guard precedes strict equality: OR-derived evidence may hide changed flags.
    const auto guard=lh_projection_guard(previous && previous->prepared_evidence ? &old : nullptr,&current);
    if(!final_typography_changed(previous,final,inputs)) return guard==LHProjectionGuard::stable ? 0u : 19u;
    if(!previous || !previous->inputs || !previous->prepared_evidence ||
        !typography_final_relation(*previous->prepared_evidence,previous->evidence) ||
        !typography_final_relation(prepared,final)) return 19;
    auto result=compose_typography(*previous->prepared_evidence,prepared,old,current);
    const auto& before=*previous->inputs;
    const auto certified=[](const style_source_accounting_s& a){return a.visual_losses_certified() && a.inputs_accounted && !a.external_pending;};
    const auto gap=[](const TypographyEvidence& e,const style_source_accounting_s& a){return bool(e.coverage_gaps&4u)==bool(a.losses || !a.inputs_accounted);};
    const bool bilateral=certified(before) && certified(inputs);
    result.add(before.inputs_accounted!=inputs.inputs_accounted || before.external_pending!=inputs.external_pending ||
        before.attribution.complete!=inputs.attribution.complete || before.attribution_consistent()!=inputs.attribution_consistent());
    if(before.losses!=inputs.losses || !(before.attribution==inputs.attribution) ||
        ((previous->evidence.coverage_gaps^final.coverage_gaps)&4u)) result.add(!bilateral);
    result.add(!gap(previous->evidence,before) || !gap(final,inputs) || (result.noop_changed && !bilateral));
    return result.impact();
}
} // namespace aether::style
