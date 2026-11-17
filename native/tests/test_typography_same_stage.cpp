// A2 production-comparator units. No observer or reusable-payload dependency.
#include "../src/style_runtime/typography_record.h"
#include "../src/style_runtime/typography_composition.h"
#include <cstdio>
#include <cstdlib>
#include "same_stage_assertions.h"
using namespace aether::style;
#define CHECK(x) SS_REQUIRE(x)
static void broad_units() {
    TypographyEvidence p; style_source_accounting_s a;
    TypographyRecord old;
    old.prepared_evidence=std::make_shared<const TypographyEvidence>(p);
    old.evidence=p; old.inputs=std::make_shared<const style_source_accounting_s>(a);
    CHECK(!prepared_typography_changed(&old,p));
    CHECK(!final_typography_changed(&old,p,a));
    CHECK(prepared_typography_changed(nullptr,p));
    CHECK(final_typography_changed(nullptr,p,a));
    auto missing=old; missing.prepared_evidence.reset();
    CHECK(prepared_typography_changed(&missing,p));
    auto missing_inputs=old;missing_inputs.inputs.reset();CHECK(final_typography_changed(&missing_inputs,p,a));
#define CHANGE(...) {auto next=p;__VA_ARGS__;CHECK(prepared_typography_changed(&old,next));CHECK(final_typography_changed(&old,next,a));CHECK(prepared_typography_visual_delta(&old,next)==TypographyVisualDelta::conservative);CHECK(final_typography_visual_delta(&old,next,a)==TypographyVisualDelta::conservative);}
    CHANGE(next.family_state=TypographyFieldState::known);
    CHANGE(next.style_state=TypographyFieldState::unsupported);
    CHANGE(next.spacing_state=TypographyFieldState::known);
    CHANGE(next.size_state=TypographyFieldState::known);
    CHANGE(next.weight_state=TypographyFieldState::known);
    CHANGE(next.line_height_state=TypographyFieldState::known);
    CHANGE(next.families={{"A",false},{"serif",true}});
    CHANGE(next.font_style="italic"); CHANGE(next.spacing_normal=true);
    CHANGE(next.spacing_px=1); CHANGE(next.size_px=20); CHANGE(next.weight=700);
    CHANGE(next.line_height_kind=TypographyLineHeightKind::number);
    CHANGE(next.line_height_value=2); CHANGE(next.line_height_used_px=32);
    CHANGE(next.size_dependencies=1); CHANGE(next.line_height_dependencies=1);
    CHANGE(next.coverage_complete=true); CHANGE(next.coverage_gaps=3);
    CHANGE(next.coverage_gaps=0x80000001u);
    for(int i=0;i<6;++i) {CHANGE(next.provenance[i].accepted_source="retained");}
    for(int i=0;i<2;++i) {
        CHANGE(next.break_policy.fields[i].known=true);
        CHANGE(next.break_policy.fields[i].computed=1);
        CHANGE(next.break_policy.fields[i].reason=BreakPolicyReason::source_limit);
        CHANGE(next.break_policy.fields[i].authored_property="alias");
        CHANGE(next.break_policy.fields[i].provenance.inherited=true);
    }
    for(auto member:{&TypographyEvidence::family_state,&TypographyEvidence::style_state,
        &TypographyEvidence::spacing_state,&TypographyEvidence::size_state,
        &TypographyEvidence::weight_state,&TypographyEvidence::line_height_state}) {
        for(auto state:{TypographyFieldState::unknown,TypographyFieldState::known,TypographyFieldState::unsupported}) {
            auto prior=p;prior.*member=state;auto record=old;
            record.prepared_evidence=std::make_shared<const TypographyEvidence>(prior);record.evidence=prior;
            for(auto next_state:{TypographyFieldState::unknown,TypographyFieldState::known,TypographyFieldState::unsupported}) {
                auto next=prior;next.*member=next_state;
                CHECK(prepared_typography_changed(&record,next)==(state!=next_state));
                CHECK(final_typography_changed(&record,next,a)==(state!=next_state));
            }
        }
    }
    for(int i=0;i<6;++i) {
        CHANGE(next.provenance[i].declaration=true);CHANGE(next.provenance[i].inherited=true);
        CHANGE(next.provenance[i].important=true);CHANGE(next.provenance[i].inline_style=true);
        CHANGE(next.provenance[i].layer=1);CHANGE(next.provenance[i].source_order=1);
        CHANGE(next.provenance[i].declaration_order=1);CHANGE(next.provenance[i].resolved_source="x");
    }
    // Equal used_px is not equal semantic line-height or future dependency.
    auto lh=p;lh.size_px=16;lh.line_height_used_px=32;lh.line_height_kind=TypographyLineHeightKind::number;lh.line_height_value=2;
    auto lh_old=old;lh_old.prepared_evidence=std::make_shared<const TypographyEvidence>(lh);lh_old.evidence=lh;
    auto px=lh;px.line_height_kind=TypographyLineHeightKind::absolute_css_px;px.line_height_value=32;
    CHECK(prepared_typography_changed(&lh_old,px));CHECK(final_typography_changed(&lh_old,px,a));
    px=lh;px.line_height_dependencies=1;CHECK(prepared_typography_changed(&lh_old,px));CHECK(final_typography_changed(&lh_old,px,a));
    auto families=p;families.families={{"A",false},{"serif",true}};
    auto family_old=old;family_old.prepared_evidence=std::make_shared<const TypographyEvidence>(families);
    auto changed=families;std::swap(changed.families[0],changed.families[1]);
    CHECK(prepared_typography_changed(&family_old,changed));
    changed=families;changed.families[0].generic=true;CHECK(prepared_typography_changed(&family_old,changed));
    // F equality cannot erase a P difference masked by final OR 4.
    old.evidence.coverage_gaps=5;auto masked=p;masked.coverage_gaps=5;
    CHECK(prepared_typography_changed(&old,masked));
    CHECK(!final_typography_changed(&old,masked,a));
    for(auto member : {&style_source_accounting_s::source_bytes,&style_source_accounting_s::declarations,
        &style_source_accounting_s::rules,&style_source_accounting_s::compiled_rules,
        &style_source_accounting_s::expanded_declarations,&style_source_accounting_s::compiled_bytes}) {
        auto next=a;next.*member=13;
        CHECK(!final_typography_changed(&old,masked,next));

    }
    auto validity=a;validity.losses=2;
    old.inputs=std::make_shared<const style_source_accounting_s>(validity);
    validity.losses=8;CHECK(final_typography_changed(&old,masked,validity));
    validity.losses=0x80000002u;CHECK(final_typography_changed(&old,masked,validity));
    validity=*old.inputs;validity.inputs_accounted=false;CHECK(final_typography_changed(&old,masked,validity));
    validity=*old.inputs;validity.external_pending=true;CHECK(final_typography_changed(&old,masked,validity));
    old.inputs=std::make_shared<const style_source_accounting_s>(a);
    validity=a;validity.external_pending=true;CHECK(final_typography_changed(&old,masked,validity));
    validity=a;validity.add(validity.source_bytes,style_source_accounting_s::max_source_bytes,style_source_accounting_s::max_source_bytes);
    CHECK(!final_typography_changed(&old,masked,validity));
    validity.add(validity.source_bytes,1,style_source_accounting_s::max_source_bytes);
    CHECK(validity.source_bytes==style_source_accounting_s::max_source_bytes);
    CHECK(final_typography_changed(&old,masked,validity));
    old.inputs=std::make_shared<const style_source_accounting_s>(validity);
    CHECK(!final_typography_changed(&old,masked,validity));
}

// Discriminant cases run independently through AETHER_ASSERT_CASE, so an
// unrelated earlier assertion cannot be misclassified as the intended kill.
static void named_units() {
 ss::run("unit-p-masked",[]{
    TypographyEvidence p;TypographyRecord old;style_source_accounting_s a;
    old.prepared_evidence=std::make_shared<const TypographyEvidence>(p);
    old.evidence=p;old.evidence.coverage_gaps=5;
    old.inputs=std::make_shared<const style_source_accounting_s>(a);
    auto next=p;next.coverage_gaps=5;
    SS_PRECONDITION("F-identical",old.evidence==next);
    SS_CHECK("P-change",prepared_typography_changed(&old,next));
    SS_CHECK("F-no-change",!final_typography_changed(&old,next,a));
 });
 const char* ids[]={"unit-loss-reason","unit-accounted","unit-pending"};
 for(int kind=0;kind<3;++kind)ss::run(ids[kind],[&]{
    TypographyRecord old;style_source_accounting_s a;
    if(kind==0)a.losses=2;
    old.inputs=std::make_shared<const style_source_accounting_s>(a);
    auto next=a;if(kind==0)next.losses=8;else if(kind==1)next.inputs_accounted=false;else next.external_pending=true;
    SS_CHECK("validity-change",final_typography_changed(&old,old.evidence,next));
 });
 ss::run("unit-used-px",[]{
    TypographyRecord old;style_source_accounting_s a;TypographyEvidence p;
    p.size_px=16;p.line_height_used_px=32;p.line_height_kind=TypographyLineHeightKind::number;p.line_height_value=2;
    old.prepared_evidence=std::make_shared<const TypographyEvidence>(p);old.evidence=p;
    old.inputs=std::make_shared<const style_source_accounting_s>(a);
    auto next=p;next.line_height_kind=TypographyLineHeightKind::absolute_css_px;next.line_height_value=32;
    SS_PRECONDITION("used-px-equal",next.line_height_used_px==p.line_height_used_px);
    SS_CHECK("P-kind-change",prepared_typography_changed(&old,next));
    SS_CHECK("F-kind-change",final_typography_changed(&old,next,a));
 });
}
static void line_height_units() {
    ss::run("unit-lh-visual",[]{
        using D=TypographyVisualDelta;
        TypographyEvidence p; p.line_height_state=TypographyFieldState::known;
        p.size_px=16; p.line_height_kind=TypographyLineHeightKind::number;
        p.line_height_value=2; p.line_height_used_px=32; p.line_height_dependencies=1;
        TypographyRecord old; style_source_accounting_s a;
        old.prepared_evidence=std::make_shared<const TypographyEvidence>(p);
        old.evidence=p; old.inputs=std::make_shared<const style_source_accounting_s>(a);
        auto px=p; px.line_height_kind=TypographyLineHeightKind::absolute_css_px;
        px.line_height_value=32; px.line_height_dependencies=0;
        px.provenance[5].resolved_source="32px";
        CHECK(prepared_typography_changed(&old,px)); CHECK(final_typography_changed(&old,px,a));
        CHECK(prepared_typography_visual_delta(&old,px)==D::line_height_only);
        CHECK(final_typography_visual_delta(&old,px,a)==D::line_height_only);
        CHECK(typography_visual_delta(px,p)==D::line_height_only);
        CHECK(prepared_typography_visual_delta(&old,p)==D::none);
        CHECK(final_typography_visual_delta(&old,p,a)==D::none);
        auto equivalent=p; equivalent.provenance[5].resolved_source="2.0";
        CHECK(typography_visual_delta(p,equivalent)==D::line_height_only);
        CHECK(prepared_typography_visual_delta(nullptr,px)==D::conservative);
        CHECK(final_typography_visual_delta(nullptr,px,a)==D::conservative);
        auto absent=old; absent.prepared_evidence.reset(); absent.inputs.reset();
        CHECK(prepared_typography_visual_delta(&absent,px)==D::conservative);
        CHECK(final_typography_visual_delta(&absent,px,a)==D::conservative);
#define MIX(...) {auto next=px;__VA_ARGS__;CHECK(prepared_typography_visual_delta(&old,next)==D::conservative);CHECK(final_typography_visual_delta(&old,next,a)==D::conservative);}
        MIX(next.line_height_kind=static_cast<TypographyLineHeightKind>(99));
        MIX(next.size_px=20); MIX(next.size_dependencies=2);
        MIX(next.provenance[3].resolved_source="16.0px");
        MIX(next.coverage_gaps|=2); MIX(next.coverage_gaps|=4);
        MIX(next.coverage_complete=true); MIX(next.break_policy.fields[0].computed=1);
        MIX(next.line_height_state=TypographyFieldState::unknown);
        MIX(next.line_height_state=TypographyFieldState::unsupported);
#undef MIX
        // P and F remain independent even when final OR4 hides a P gap change.
        old.evidence.coverage_gaps=5; auto masked=p; masked.coverage_gaps=5;
        CHECK(prepared_typography_visual_delta(&old,masked)==D::conservative);
        CHECK(final_typography_visual_delta(&old,masked,a)==D::none);
        a.losses=2; old.inputs=std::make_shared<const style_source_accounting_s>(a);
        for(int i=0;i<3;++i) {
            auto changed=a;
            if(i==0)changed.losses=8;
            if(i==1)changed.inputs_accounted=false;
            if(i==2)changed.external_pending=true;
            CHECK(final_typography_changed(&old,masked,changed));
            CHECK(final_typography_visual_delta(&old,masked,changed)==D::conservative);
            CHECK(prepared_typography_visual_delta(&old,p)==D::none);
        }
        // Identical conservative states do not repeatedly inject paint.
        for(auto state:{TypographyFieldState::unknown,TypographyFieldState::known,TypographyFieldState::unsupported}) {
            for(uint32_t gap:{1u,3u,5u,0x80000001u}) {
                auto stable=p; stable.line_height_state=state; stable.coverage_gaps=gap;
                old.prepared_evidence=std::make_shared<const TypographyEvidence>(stable);old.evidence=stable;
                CHECK(prepared_typography_visual_delta(&old,stable)==D::none);
                CHECK(final_typography_visual_delta(&old,stable,a)==D::none);
                auto restored=stable; restored.coverage_gaps^=2;
                CHECK(prepared_typography_visual_delta(&old,restored)==D::conservative);
                CHECK(final_typography_visual_delta(&old,restored,a)==D::conservative);
            }
        }
    });
}
static void nonvisual_units() {
 ss::run("unit-attribution",[]{
    using D=TypographyVisualDelta;
    TypographyEvidence p; auto clean=style_source_accounting_s::tracked();
    TypographyRecord old; old.prepared_evidence=std::make_shared<const TypographyEvidence>(p);
    old.evidence=p; old.inputs=std::make_shared<const style_source_accounting_s>(clean);
    auto width=clean; width.reject_direct_width(); auto gap=p; gap.coverage_gaps|=4;
    CHECK(width.losses==2 && width.visual_losses_certified());
    CHECK(final_typography_changed(&old,gap,width));
    CHECK(final_typography_visual_delta(&old,gap,width)==D::computed_noop);
    old.evidence=gap;old.inputs=std::make_shared<const style_source_accounting_s>(width);
    CHECK(final_typography_visual_delta(&old,p,clean)==D::computed_noop);
    auto twice=width;twice.reject_direct_width();
    CHECK(final_typography_changed(&old,gap,twice));
    CHECK(final_typography_visual_delta(&old,gap,twice)==D::computed_noop);
    auto partial=width;partial.loss(style_source_loss::rejected_declaration);
    CHECK(partial.losses==width.losses && !partial.attribution.complete);
    CHECK(final_typography_changed(&old,gap,partial));
    CHECK(!prepared_typography_changed(&old,p));
    CHECK(final_typography_visual_delta(&old,gap,partial)==D::conservative);
    auto no_certificate=width;no_certificate.attribution.complete=false;
    CHECK(final_typography_changed(&old,gap,no_certificate));
    CHECK(final_typography_visual_delta(&old,gap,no_certificate)==D::conservative);
    old.inputs=std::make_shared<const style_source_accounting_s>(no_certificate);
    CHECK(final_typography_changed(&old,gap,width));
    CHECK(final_typography_visual_delta(&old,gap,width)==D::conservative);
    CHECK(!final_typography_changed(&old,gap,no_certificate));
    CHECK(final_typography_visual_delta(&old,gap,no_certificate)==D::none);
    CHECK(final_typography_visual_delta(&old,gap,no_certificate)==D::none);
    old.inputs=std::make_shared<const style_source_accounting_s>(partial);
    CHECK(!final_typography_changed(&old,gap,partial));
    CHECK(final_typography_visual_delta(&old,gap,partial)==D::none);
    auto other_partial=partial;++other_partial.attribution.direct_width_rejections;
    CHECK(final_typography_changed(&old,gap,other_partial));
    CHECK(final_typography_visual_delta(&old,gap,other_partial)==D::conservative);
    for(bool reverse:{false,true}) {
        auto merged=style_source_accounting_s::tracked();
        merged.merge(reverse?partial:width); merged.merge(reverse?width:partial); merged.merge(clean);
        CHECK(merged.losses==2 && !merged.visual_losses_certified());
        CHECK(merged.attribution.direct_width_rejections==2);
        old.inputs=std::make_shared<const style_source_accounting_s>(width);
        CHECK(final_typography_changed(&old,gap,merged));
        CHECK(final_typography_visual_delta(&old,gap,merged)==D::conservative);
    }
    auto absent=style_source_accounting_s{}; auto merged=clean;merged.merge(absent);
    CHECK(!merged.attribution.complete && merged.losses==0);
    auto corrupt=width;corrupt.losses=8;
    CHECK(!corrupt.attribution_consistent());
    auto overflow=width;overflow.attribution.direct_width_rejections=overflow.max_declarations;
    overflow.reject_direct_width();CHECK(!overflow.attribution.complete && !overflow.inputs_accounted);
    auto merge_overflow=width;merge_overflow.merge(overflow);CHECK(!merge_overflow.visual_losses_certified());
    for(auto reason:{style_source_loss::external_source,style_source_loss::budget,style_source_loss::unknown_declaration}) {
        auto bad=width;bad.loss(reason);CHECK(!bad.visual_losses_certified());
        CHECK(final_typography_visual_delta(&old,gap,bad)==D::conservative);
    }
    auto missing=old;missing.inputs.reset();CHECK(final_typography_visual_delta(&missing,gap,width)==D::conservative);
 });
 ss::run("unit-noop-slots",[]{
    using D=TypographyVisualDelta;
    TypographyEvidence p;p.noop_absence.fill(NoopAbsenceProof::local_absence);
    auto a=style_source_accounting_s::tracked();TypographyRecord old;
    old.prepared_evidence=std::make_shared<const TypographyEvidence>(p);old.evidence=p;
    old.inputs=std::make_shared<const style_source_accounting_s>(a);
    auto style=p;style.noop_absence[0]=NoopAbsenceProof::unproven;style.style_state=TypographyFieldState::known;style.font_style="italic";
    style.provenance[1].declaration=true;style.provenance[1].accepted_source="italic";
    auto spacing=p;spacing.noop_absence[1]=NoopAbsenceProof::unproven;spacing.spacing_state=TypographyFieldState::known;spacing.spacing_px=3;
    spacing.provenance[2].declaration=true;spacing.provenance[2].accepted_source="3px";
    auto both=style;both.noop_absence[1]=NoopAbsenceProof::unproven;both.spacing_state=spacing.spacing_state;both.spacing_px=3;both.provenance[2]=spacing.provenance[2];
    for(const auto& next:{style,spacing,both}) {
        CHECK(prepared_typography_changed(&old,next));CHECK(final_typography_changed(&old,next,a));
        CHECK(prepared_typography_visual_delta(&old,next)==D::computed_noop);
        CHECK(final_typography_visual_delta(&old,next,a)==D::computed_noop);
        CHECK(typography_visual_delta(next,p)==D::computed_noop);
        auto unsupported=next;unsupported.style_state=TypographyFieldState::unsupported;
        CHECK(prepared_typography_visual_delta(&old,unsupported)==D::conservative);
        auto size=next;size.size_px=20;CHECK(prepared_typography_visual_delta(&old,size)==D::conservative);
        auto gaps=next;gaps.coverage_gaps|=2;CHECK(prepared_typography_visual_delta(&old,gaps)==D::conservative);
    }
    auto unproved=both;unproved.provenance[1].accepted_source.clear();
    CHECK(prepared_typography_visual_delta(&old,unproved)==D::conservative);
    auto width=a;width.reject_direct_width();both.coverage_gaps|=4;
    CHECK(final_typography_visual_delta(&old,both,width)==D::computed_noop);
    auto pending=width;pending.external_pending=true;
    CHECK(final_typography_visual_delta(&old,both,pending)==D::conservative);
    auto retained=old.inputs;old.inputs=std::make_shared<const style_source_accounting_s>(width);
    CHECK(retained->losses==0 && retained->attribution.direct_width_rejections==0);
 });
}
static void absence_units() {
 ss::run("unit-noop-absence-proof",[]{
    using D=TypographyVisualDelta;
    auto inputs=style_source_accounting_s::tracked();
    const auto check=[&](const TypographyEvidence& before,const TypographyEvidence& next,D expected) {
        TypographyRecord old;old.prepared_evidence=std::make_shared<const TypographyEvidence>(before);
        old.evidence=before;old.inputs=std::make_shared<const style_source_accounting_s>(inputs);
        CHECK(prepared_typography_changed(&old,next)==(before!=next));
        CHECK(final_typography_changed(&old,next,inputs)==(before!=next));
        CHECK(prepared_typography_visual_delta(&old,next)==expected);
        CHECK(final_typography_visual_delta(&old,next,inputs)==expected);
    };
    for(size_t slot=0;slot<2;++slot) {
        TypographyEvidence missing,toggled;toggled.provenance[slot+1].inherited=true;
        check(missing,toggled,D::conservative);check(toggled,missing,D::conservative);check(toggled,toggled,D::none);
        auto local=missing;local.noop_absence.fill(NoopAbsenceProof::local_absence);
        CHECK(local.noop_absence_valid(slot));
        auto flag=local;flag.provenance[slot+1].inherited=true;
        CHECK(!flag.noop_absence_valid(slot));check(local,flag,D::conservative);check(flag,local,D::conservative);
        auto inherited=flag;inherited.noop_absence[slot]=NoopAbsenceProof::inherited_absence;
        CHECK(inherited.noop_absence_valid(slot));check(local,inherited,D::conservative);check(inherited,local,D::conservative);
        check(inherited,inherited,D::none);check(missing,local,D::conservative);check(local,missing,D::conservative);
        auto known=local;known.noop_absence[slot]=NoopAbsenceProof::unproven;
        known.provenance[slot+1].declaration=true;known.provenance[slot+1].accepted_source=slot?"3px":"italic";
        if(slot){known.spacing_state=TypographyFieldState::known;known.spacing_px=3;}
        else{known.style_state=TypographyFieldState::known;known.font_style="italic";}
        check(local,known,D::computed_noop);check(inherited,known,D::computed_noop);check(known,inherited,D::computed_noop);
        auto residual=known;residual.noop_absence[slot]=NoopAbsenceProof::local_absence;
        check(local,residual,D::conservative);check(known,residual,D::conservative);
        auto parsed=local;
        if(slot)typography_parse_spacing("garbage",parsed);else typography_parse_style("garbage",parsed);
        CHECK(parsed.noop_absence[slot]==NoopAbsenceProof::unproven);check(local,parsed,D::conservative);
    }
 });
}
// Compositional contract: no base mask is an input to these assertions.
static void composition_units() {
 ss::run("unit-composition",[]{
    TypographyEvidence a; a.coverage_gaps=0; a.noop_absence.fill(NoopAbsenceProof::local_absence);
    a.family_state=a.size_state=a.weight_state=a.line_height_state=TypographyFieldState::known;
    a.families={{"Arial",false}}; a.size_px=16; a.weight=400;
    a.line_height_kind=TypographyLineHeightKind::number; a.line_height_value=1;
    a.line_height_used_px=16; a.line_height_dependencies=MEASURE_SELF_FONT;
    for(size_t i:{size_t(0),size_t(3),size_t(4),size_t(5)}) {
        a.provenance[i].declaration=true;a.provenance[i].accepted_source="value";
        if(i) a.provenance[i].resolved_source="value";
    }
    a.provenance[0].accepted_source="Arial";
    TypographyProjection p{"Arial",16,400,{measure_line_height_kind_e::number,1},16,0,0,16};
    const auto clean=style_source_accounting_s::tracked();
    const auto record=[&](const TypographyEvidence& e,const style_source_accounting_s& inputs){
        TypographyRecord r;r.prepared_evidence=std::make_shared<const TypographyEvidence>(e);
        r.evidence=e;if(inputs.losses || !inputs.inputs_accounted)r.evidence.coverage_gaps|=4;
        r.inputs=std::make_shared<const style_source_accounting_s>(inputs);return r;
    };
    const auto check=[&](const TypographyEvidence& b,const TypographyProjection& q,uint32_t expected){
        auto old=record(a,clean);auto reverse=record(b,clean);
        CHECK(composed_prepared_impact(&old,b,p,q)==expected);
        CHECK(composed_final_impact(&old,b,b,clean,p,q)==expected);
        CHECK(composed_prepared_impact(&reverse,a,q,p)==expected);
        CHECK(composed_final_impact(&reverse,a,a,clean,q,p)==expected);
    };
    auto b=a;auto q=p;check(b,q,0);
    // Break-policy evidence is not certified by equal scalar projections.
    // Check each field and provenance component independently, both directions.
    for(size_t slot=0;slot<2;++slot)for(unsigned mutation=0;mutation<13;++mutation){
        auto changed=a;auto& field=changed.break_policy.fields[slot];
        switch(mutation){
        case 0:field.computed=2;break;
        case 1:field.authored_property="word-wrap";break;
        case 2:field.known=!field.known;break;
        case 3:field.reason=BreakPolicyReason::variable_unproven;break;
        case 4:field.provenance.declaration=true;break;
        case 5:field.provenance.inherited=true;break;
        case 6:field.provenance.important=true;break;
        case 7:field.provenance.inline_style=true;break;
        case 8:field.provenance.layer=1;break;
        case 9:field.provenance.source_order=1;break;
        case 10:field.provenance.declaration_order=1;break;
        case 11:field.provenance.accepted_source="break-word";break;
        case 12:field.provenance.resolved_source="break-word";break;
        }
        const char* mutations[]={"computed","authored-property","known","reason","declaration","inherited","important","inline-style","layer","source-order","declaration-order","accepted-source","resolved-source"};
        const auto label=std::string("catalog.unit.")+(slot?"overflow-wrap.":"word-break.")+mutations[mutation]+".";
        const auto assert_layer=[&](const char* phase,bool condition){SS_CHECK((label+phase).c_str(),condition);};
        const auto original=record(a,clean),stable=record(changed,clean);
        assert_layer("change.P",composed_prepared_impact(&original,changed,p,p)==19);
        assert_layer("change.F",composed_final_impact(&original,changed,changed,clean,p,p)==19);
        assert_layer("inverse.P",composed_prepared_impact(&stable,a,p,p)==19);
        assert_layer("inverse.F",composed_final_impact(&stable,a,a,clean,p,p)==19);
        assert_layer("repeat.P",composed_prepared_impact(&stable,changed,p,p)==0);
        assert_layer("repeat.F",composed_final_impact(&stable,changed,changed,clean,p,p)==0);
        // A represented family change cannot cancel the independent residual.
        auto mixed=changed;auto projection=p;mixed.families={{"monospace",true}};
        mixed.provenance[0].accepted_source="monospace";projection.family="monospace";check(mixed,projection,19);
        mixed.coverage_gaps|=2;check(mixed,projection,19);
        mixed.family_state=TypographyFieldState::unknown;check(mixed,projection,19);
        projection.lh_deps=MEASURE_ROOT_FONT;check(mixed,projection,19);
    }

    // Every pair of represented groups, plus each group alone.
    for(unsigned mask=1;mask<16;++mask) {
        b=a;q=p;
        if(mask&1){b.families={{"monospace",true}};b.provenance[0].accepted_source="monospace";q.family="monospace";}
        if(mask&2){b.size_px=20;q.size=20;q.self_font_size=20;b.line_height_used_px=20;q.used=20;b.provenance[3].resolved_source="20px";}
        if(mask&4){b.weight=700;q.weight=700;b.provenance[4].resolved_source="700";}
        if(mask&8){b.line_height_value=2;q.lh.value=2;b.line_height_used_px=q.size*2;q.used=q.size*2;b.provenance[5].resolved_source="2";}
        check(b,q,0);
        for(size_t slot=0;slot<2;++slot) {
            auto mix=b;mix.noop_absence[slot]=NoopAbsenceProof::unproven;
            mix.provenance[slot+1].declaration=true;mix.provenance[slot+1].accepted_source=slot?"3px":"italic";
            if(slot){mix.spacing_state=TypographyFieldState::known;mix.spacing_px=3;}
            else{mix.style_state=TypographyFieldState::known;mix.font_style="italic";}
            check(mix,q,0);mix.coverage_gaps|=2;check(mix,q,19);
        }
        auto uncertain=b;uncertain.provenance[0].source_order++;check(uncertain,q,19);
    }
    b=a;b.families={{"monospace",true}};b.provenance[0].accepted_source="monospace";q=p;q.family="monospace";
    auto old=record(a,clean);
    const auto reset_old=[&](const style_source_accounting_s& inputs){auto r=record(a,inputs);old.prepared_evidence=r.prepared_evidence;old.evidence=r.evidence;old.inputs=r.inputs;};
    CHECK((82u|composed_prepared_impact(&old,b,p,q))==82);
    auto wrong=p;wrong.family="wrong";
    CHECK(composed_prepared_impact(&old,b,wrong,q)==19);
    wrong=q;wrong.family="wrong";CHECK(composed_prepared_impact(&old,b,p,wrong)==19);
    CHECK(composed_prepared_impact(nullptr,b,p,q)==19);
    CHECK(composed_final_impact(nullptr,b,b,clean,p,q)==19);
    old.prepared_evidence.reset();CHECK(composed_final_impact(&old,b,b,clean,p,q)==19);
    // Loss/proof transitions compose with represented and nonvisual changes.
    b.style_state=TypographyFieldState::known;b.font_style="italic";b.noop_absence[0]=NoopAbsenceProof::unproven;
    b.provenance[1].declaration=true;b.provenance[1].accepted_source="italic";
    auto width=clean;width.reject_direct_width();auto final=b;final.coverage_gaps|=4;
    reset_old(clean);CHECK(composed_final_impact(&old,b,final,width,p,q)==0);
    auto partial=width;partial.loss(style_source_loss::rejected_declaration);
    reset_old(width);CHECK(composed_final_impact(&old,b,final,partial,p,q)==19);
    reset_old(clean);old.evidence.coverage_gaps|=4;
    CHECK(composed_final_impact(&old,b,final,clean,p,q)==19); // stable invented gap
    reset_old(clean);old.evidence.weight++;
    CHECK(composed_final_impact(&old,b,b,clean,p,q)==19); // broken P/F relation
    reset_old(clean);auto pending=clean;pending.external_pending=true;
    CHECK(composed_final_impact(&old,b,b,pending,p,q)==19);
    reset_old(partial);CHECK(composed_final_impact(&old,a,old.evidence,partial,p,p)==0);
    // Dependencies/kind and coverage cannot be washed out by represented family.
    b=a;q=p;b.size_dependencies=MEASURE_PARENT; q.size_deps=MEASURE_PARENT;check(b,q,19);
    b=a;q=p;b.line_height_dependencies|=MEASURE_PARENT;q.lh_deps=MEASURE_PARENT;check(b,q,19);
    b=a;q=p;b.line_height_kind=TypographyLineHeightKind::absolute_css_px;b.line_height_value=16;
    b.line_height_dependencies=0;q.lh={measure_line_height_kind_e::absolute_css_px,16};check(b,q,0);
    b=a;q=p;b.line_height_value=2;b.line_height_used_px=32;q.lh.value=2;q.used=32;
    check(b,q,0);reset_old(clean);CHECK((81u|composed_prepared_impact(&old,b,p,q))==81);
    b=a;b.weight=901;q=p;q.weight=901;check(b,q,19);
    b=a;b.break_policy.fields[0].known=true;check(b,p,19);
    b=a;b.coverage_complete=true;check(b,p,19);
    b=a;b.provenance[3].accepted_source="16px";check(b,p,0); // strict publication, no visual work
    CHECK(prepared_typography_changed(&old,b));
    // Family residual profiles, including equal scalar prefixes.
    for(const std::string& source:std::vector<std::string>{"Arial,serif","\"Arial\"","A\\72 ial","default",std::string(64,'A')}) {
        b=a;b.provenance[0].accepted_source=source;b.families={{source,false}};check(b,p,19);
    }
    b=a;b.families.push_back({"serif",true});check(b,p,19);
    b=a;b.families[0].generic=true;check(b,p,19);
    b=a;b.provenance[0].resolved_source="Arial";check(b,p,19);
    // P unchanged: accounting alone must discriminate certified from partial F.
    reset_old(clean); auto only_gap=a;only_gap.coverage_gaps|=4;
    CHECK(!prepared_typography_changed(&old,a));CHECK(final_typography_changed(&old,only_gap,width));
    CHECK(composed_prepared_impact(&old,a,p,p)==0);
    CHECK(composed_final_impact(&old,a,only_gap,width,p,p)==0);
    CHECK(composed_final_impact(&old,a,only_gap,partial,p,p)==19);
    reset_old(width);CHECK(composed_final_impact(&old,a,a,clean,p,p)==0);
    CHECK(composed_final_impact(&old,a,only_gap,width,p,p)==0);
    // Each changed projection is contradicted on either side independently.
    for(unsigned group=0;group<3;++group) for(unsigned fault=0;fault<6;++fault) for(bool left:{false,true}) {
        auto before=a,after=a;auto op=p,np=p;
        after.families={{"monospace",true}};after.provenance[0].accepted_source="monospace";np.family="monospace";
        if(group==0){after.size_px=20;np.size=20;np.self_font_size=20;after.line_height_used_px=20;np.used=20;}
        if(group==1){after.weight=700;np.weight=700;}
        if(group==2){after.line_height_value=2;np.lh.value=2;after.line_height_used_px=32;np.used=32;}
        auto& e=left?before:after;auto& v=left?op:np;
        if(group==0){
            if(fault==0)v.size+=1;if(fault==1)e.size_px=std::numeric_limits<float>::quiet_NaN();
            if(fault==2)e.size_px=0;if(fault==3)v.size_deps|=MEASURE_PARENT;
            if(fault==4)e.provenance[3].resolved_source.clear();if(fault==5)e.size_state=TypographyFieldState::unsupported;
        } else if(group==1){
            if(fault==0)v.weight+=1;if(fault==1)e.weight=0;if(fault==2)e.weight=v.weight=901;
            if(fault==3)e.provenance[4].resolved_source.clear();if(fault==4)e.weight_state=TypographyFieldState::unknown;
            if(fault==5)e.provenance[4].accepted_source.clear();
        } else {
            if(fault==0)v.lh.kind=measure_line_height_kind_e::absolute_css_px;
            if(fault==1)v.lh.value+=1;if(fault==2)v.used+=1;if(fault==3)v.lh_deps|=MEASURE_PARENT;
            if(fault==4)e.line_height_value=std::numeric_limits<float>::quiet_NaN();
            if(fault==5)e.line_height_state=TypographyFieldState::unsupported;
        }
        auto r=record(before,clean),rev=record(after,clean);
        CHECK(composed_prepared_impact(&r,after,op,np)==19);
        CHECK(composed_final_impact(&r,after,after,clean,op,np)==19);
        CHECK(composed_prepared_impact(&rev,before,np,op)==19);
        CHECK(composed_final_impact(&rev,before,before,clean,np,op)==19);
    }
    // Equal scalars with genuinely distinct full evidence at BOTH endpoints.
    const auto collision=[&](TypographyEvidence before,TypographyEvidence after,const char* scalar,uint32_t expected){
        auto op=p,np=p;op.family=np.family=scalar;
        auto r=record(before,clean),rev=record(after,clean);
        CHECK(before!=after);CHECK(std::strcmp(op.family,np.family)==0);
        CHECK(composed_prepared_impact(&r,after,op,np)==expected);
        CHECK(composed_final_impact(&r,after,after,clean,op,np)==expected);
        CHECK(composed_prepared_impact(&rev,before,np,op)==expected);
        CHECK(composed_final_impact(&rev,before,before,clean,np,op)==expected);
    };
    auto list_a=a,list_b=a;list_a.families.push_back({"serif",true});list_b.families.push_back({"monospace",true});
    list_a.provenance[0].accepted_source="Arial,serif";list_b.provenance[0].accepted_source="Arial,monospace";
    collision(list_a,list_b,"Arial",19);
    list_a.families.push_back({"monospace",true});list_b.families.push_back({"serif",true});
    list_a.provenance[0].accepted_source="Arial,serif,monospace";list_b.provenance[0].accepted_source="Arial,monospace,serif";
    collision(list_a,list_b,"Arial",19);
    const std::string prefix(63,'A');list_a=a;list_b=a;
    list_a.families={{prefix+"B",false}};list_b.families={{prefix+"C",false}};
    list_a.provenance[0].accepted_source=prefix+"B";list_b.provenance[0].accepted_source=prefix+"C";
    collision(list_a,list_b,prefix.c_str(),19);
    list_a.families={{prefix,false}};list_a.provenance[0].accepted_source=prefix;
    collision(list_a,list_b,prefix.c_str(),19);
    const std::string other(63,'B');list_b.families={{other,false}};list_b.provenance[0].accepted_source=other;
    auto lp=p,lq=p;lp.family=prefix.c_str();lq.family=other.c_str();auto limit=record(list_a,clean);
    CHECK(composed_prepared_impact(&limit,list_b,lp,lq)==0);CHECK(composed_final_impact(&limit,list_b,list_b,clean,lp,lq)==0);

    // v3/v4: real flags are independent of the OR-derived evidence dependency.
    reset_old(clean);q=p;q.lh_deps=MEASURE_SELF_FONT;
    CHECK(!prepared_typography_changed(&old,a)&&!final_typography_changed(&old,a,clean));
    CHECK(lh_projection_guard(&p,&q)==LHProjectionGuard::changed);
    CHECK(composed_prepared_impact(&old,a,p,q)==19);
    CHECK(composed_final_impact(&old,a,a,clean,p,q)==19);
    CHECK(composed_prepared_impact(&old,a,q,p)==19);
    CHECK(composed_final_impact(&old,a,a,clean,q,p)==19);
    CHECK(lh_projection_guard(&q,&q)==LHProjectionGuard::stable);
    CHECK(composed_prepared_impact(&old,a,q,q)==0&&composed_final_impact(&old,a,a,clean,q,q)==0);
    CHECK(lh_projection_guard(nullptr,&q)==LHProjectionGuard::unproven);
    CHECK(composed_prepared_impact(nullptr,a,p,p)==19&&composed_final_impact(nullptr,a,a,clean,p,p)==19);
    b=a;q=p;q.lh_deps=MEASURE_ROOT_FONT;b.line_height_dependencies=MEASURE_SELF_FONT|MEASURE_ROOT_FONT;check(b,q,19);
    b=a;q=p;b.line_height_kind=TypographyLineHeightKind::absolute_css_px;b.line_height_value=16;
    b.line_height_dependencies=0;q.lh={measure_line_height_kind_e::absolute_css_px,16};check(b,q,0);
    for(bool left:{false,true}){
        auto op=p,np=q;(left?op:np).self_font_size=17;
        CHECK(composed_prepared_impact(&old,b,op,np)==19);
        CHECK(composed_final_impact(&old,b,b,clean,op,np)==19);
        auto invalid=b;invalid.line_height_dependencies=MEASURE_SELF_FONT;
        CHECK(composed_prepared_impact(&old,invalid,p,q)==19);
    }
    auto normal=a;normal.line_height_kind=TypographyLineHeightKind::normal;normal.line_height_value=1.2f;
    normal.line_height_used_px=16*1.2f;auto normal_p=p;normal_p.lh={measure_line_height_kind_e::normal,1.2f};normal_p.used=16*1.2f;
    auto absolute=normal;absolute.line_height_kind=TypographyLineHeightKind::absolute_css_px;
    absolute.line_height_value=normal.line_height_used_px;absolute.line_height_dependencies=0;
    auto absolute_p=normal_p;absolute_p.lh={measure_line_height_kind_e::absolute_css_px,absolute.line_height_value};
    auto normal_record=record(normal,clean),absolute_record=record(absolute,clean);
    CHECK(composed_prepared_impact(&normal_record,absolute,normal_p,absolute_p)==0);
    CHECK(composed_final_impact(&normal_record,absolute,absolute,clean,normal_p,absolute_p)==0);
    CHECK(composed_prepared_impact(&absolute_record,normal,absolute_p,normal_p)==0);
    CHECK(composed_final_impact(&absolute_record,normal,normal,clean,absolute_p,normal_p)==0);
    // Stable flags permit composition, never cancel coverage/certificate residuals.
    b.style_state=TypographyFieldState::known;b.font_style="italic";b.noop_absence[0]=NoopAbsenceProof::unproven;
    b.provenance[1].declaration=true;b.provenance[1].accepted_source="italic";check(b,q,0);
    b.families={{"monospace",true}};b.provenance[0].accepted_source="monospace";q.family="monospace";check(b,q,0);
    b.coverage_gaps|=2;check(b,q,19);

    b=a;q=p;b.families={{"monospace",true}};b.provenance[0].accepted_source="monospace";q.family="monospace";
    b.style_state=TypographyFieldState::known;b.font_style="italic";b.noop_absence[0]=NoopAbsenceProof::unproven;
    b.provenance[1].declaration=true;b.provenance[1].accepted_source="italic";q.lh_deps=MEASURE_SELF_FONT;
    check(b,q,19); // hidden flag guard composes with represented + certified no-op
    for(bool left:{false,true}){
        auto op=p,np=q;np.lh_deps=0;
        (left?op:np).lh.kind=static_cast<measure_line_height_kind_e>(99);
        auto changed=b;changed.line_height_value=2;changed.line_height_used_px=32;
        np.lh.value=2;np.used=32;
        CHECK(composed_prepared_impact(&old,changed,op,np)==19);
        CHECK(composed_final_impact(&old,changed,changed,clean,op,np)==19);
    }
    // Accumulator monotonicity under every permutation; no special combination branch.
    std::array<unsigned,3> order{0,1,2};
    do {
        TypographyContribution uncertain,proven;
        for(auto group:order){uncertain.add(group==2,group==1);proven.add(false,group==2);}
        CHECK(uncertain.impact()==19 && uncertain.noop_changed);
        CHECK(proven.impact()==0 && proven.noop_changed);
    } while(std::next_permutation(order.begin(),order.end()));
 });
}

int main(){composition_units();ss::run("unit-broad",broad_units);named_units();line_height_units();nonvisual_units();absence_units();return ss::finish();}
