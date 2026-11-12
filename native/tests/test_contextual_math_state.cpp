#include "../src/css/frozen_measure_read.h"
#include "../src/css/computed_style_handle.h"
#include <aether/aether_contextual_style.h>
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace aether::style;
int main() {
    aether::css::math_tree parsed;
    assert(aether::css::parse_contextual_math("max(2em,30%)", parsed));
    auto computed = aether::css::compute_contextual_math(parsed,20,16,1280,800);
    measure_state_s state;
    state.document=std::make_shared<const document_measure_context_s>();
    const std::string property="padding-left";
    measure_apply_state_s writer(state,property);
    writer.set_tree(property,computed);
    writer.set_property_dependencies(MEASURE_PERCENTAGE|MEASURE_SELF_FONT);
    measure_recompute_dependencies(state);
    measure_property_delta_s delta;
    measure_apply_state_s probe(state,delta,property);
    assert(probe.read().tree(property)==computed);
    probe.set_tree(property,nullptr);
    assert(!probe.read().tree(property));
    assert(writer.read().tree(property)==computed);
    probe.set_tree(property,parsed);
    assert(probe.read().tree(property)==parsed);
    assert(writer.read().tree(property)==computed);
    std::shared_ptr<const FrozenDocumentMeasureContext> document;
    assert(freeze_document({},document)==AETHER_OK);
    FrozenMeasuresRef frozen;
    assert(freeze_runtime_measures(state,document,frozen)==AETHER_OK);
    writer.set_tree(property,nullptr);
    assert(!writer.read().tree(property));
    state={};document.reset();parsed.reset();computed.reset();
    MeasuresReadView view(*frozen);
    double value=0;
    assert(aether::css::resolve_contextual_math(view.contextual_tree(property),100,value)&&value==40);
    assert(aether::css::resolve_contextual_math(view.contextual_tree(property),500,value)&&value==150);
    auto* handle=new aether_computed_style_handle_s();
    handle->measures=frozen;
    aether_computed_style_t sentinel;
    std::memset(&sentinel,0x5a,sizeof sentinel);
    unsigned char before[sizeof sentinel];std::memcpy(before,&sentinel,sizeof sentinel);
    assert(aether_computed_style_copy(handle,&sentinel)==AETHER_ERROR_INVALID_STATE);
    assert(!std::memcmp(before,&sentinel,sizeof sentinel));
    aether_computed_style_handle_t clone=nullptr;
    assert(aether_computed_style_handle_clone(handle,&clone)==AETHER_OK);
    aether_computed_style_handle_destroy(handle);frozen.reset();
    assert(aether::css::resolve_contextual_math(clone->measures->contextual_tree(property),500,value)&&value==150);
    aether_computed_style_handle_destroy(clone);
    puts("contextual math state/ABI: PASS");
}
