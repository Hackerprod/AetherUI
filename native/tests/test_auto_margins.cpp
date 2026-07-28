#include <aether/aether.h>

#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "layout_types.h"

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static bool same(float actual, float expected) {
    return std::fabs(actual - expected) < 0.001f;
}

static aether_text_metrics_t measure_empty(const char* text, const aether_computed_style_t*, float, void*) {
    return {static_cast<float>(std::strlen(text)) * 10.f, 1.f, .8f};
}

static aether_layout_box_t box_for(aether_doc_t doc, aether_layout_result_t result, const char* id) {
    const auto element = aether_doc_get_element_by_id(doc, id);
    require(element != nullptr, "fixture element lookup failed");
    const auto box = aether_layout_result_find_primary_box(result, element);
    require(box != nullptr, "fixture layout box missing");
    return box;
}

int main() {
    const auto defaults = aether_computed_style_default();
    require(defaults.margin.left.unit == AETHER_CSS_UNIT_PX && same(defaults.margin.left.value, 0.f) &&
            defaults.margin.right.unit == AETHER_CSS_UNIT_PX && same(defaults.margin.right.value, 0.f),
        "computed-style default margins must be CSS 0px rather than auto");
    require(same(defaults.flex_shrink, 1.f) && defaults.flex_basis.kind == AETHER_FLEX_BASIS_AUTO,
        "computed-style defaults must preserve flex-shrink: 1 and flex-basis: auto");

    const char* markup =
        "<body>"
        "<div id='block'><div id='block-child'></div></div>"
        "<div id='vertical'><div id='vertical-child'></div></div>"
        "<div id='row-main'><div id='row-main-child'></div></div>"
        "<div id='row-cross'><div id='row-cross-child'></div></div>"
        "<div id='column-main'><div id='column-main-child'></div></div>"
        "<div id='column-cross'><div id='column-cross-child'></div></div>"
        "<div id='row-stretch-auto'><div id='row-stretch-short'>ok</div><div id='row-stretch-tall'>one two three four five six</div></div>"
        "<div id='row-stretch-fixed'><div id='row-stretch-fill'></div><div id='row-stretch-explicit'></div></div>"
        "<div id='row-align-self'><div id='row-align-self-center'></div><div id='row-align-self-end'></div></div>"
        "<div id='row-final-main'><div id='row-final-main-short'>ok</div><div id='row-final-main-wrap'>one two three four</div></div>"
        "<div id='row-stretch-wrap'><div id='row-stretch-wrap-a'>one two three</div><div id='row-stretch-wrap-b'>ok</div><div id='row-stretch-wrap-c'>one two three four five</div></div>"
        "<div id='column-stretch-auto'><div id='column-stretch-short'>ok</div><div id='column-stretch-tall'>one two three four five six</div><div id='column-stretch-explicit'></div></div>"
        "<div id='column-stretch-base'><div id='column-stretch-base-wrap'>one two three four five six</div><div id='column-stretch-base-following'></div></div>"
        "<div id='column-natural-base'><div id='column-natural-base-content'>one two three four five six</div></div>"
        "<div id='column-align-self'><div id='column-align-self-center'></div><div id='column-align-self-end'></div></div>"
        "<div id='shrink-row'><div id='shrink-a'></div><div id='shrink-b'></div></div>"
        "<div id='automatic-row'><div id='automatic-a'></div><div id='automatic-b'></div></div>"
        "<div id='min-row'><div id='min-a'></div><div id='min-b'></div></div>"
        "<div id='column-basis'><div id='column-fixed'></div><div id='column-fill'></div></div>"
        "<div id='column-shrink'><div id='column-shrink-a'></div><div id='column-shrink-b'></div></div>"
        "<div id='wrap-row'><div id='wrap-a'></div><div id='wrap-b'></div><div id='wrap-c'></div></div>"
        "<div id='text-min-row'><div id='text-min-a'>Blood Pressure</div><div id='text-min-b'>Heart Rate</div></div>"
        "<div id='mixed-inline-min-row'><div id='mixed-inline-a'>Pressure <span>x</span></div><div id='mixed-inline-b'>x</div></div>"
        "<div id='anonymous-flex-tabs'><div id='anonymous-flex-tab'>Engine.cs <span id='anonymous-flex-close'>x</span></div><div id='anonymous-flex-tab-sibling'></div></div>"
        "<div id='opaque-control-row'><input id='opaque-input' value='value'></div>"
        "<div id='flow-button-row'><button id='flow-button'>Edit One</button></div>"
        "<textarea id='multiline-textarea'>Note</textarea>"
        "<div id='overflow-y-axis'></div><div id='overflow-x-axis'></div>"
        "<div id='flex-min-visible-row'><div id='flex-min-visible-row-side'></div><div id='flex-min-visible-row-content'>unbreakable-content</div></div>"
        "<div id='flex-min-hidden-row'><div id='flex-min-hidden-row-side'></div><div id='flex-min-hidden-row-content'>unbreakable-content</div></div>"
        "<div id='flex-min-auto-row'><div id='flex-min-auto-row-side'></div><div id='flex-min-auto-row-content'>unbreakable-content</div></div>"
        "<div id='flex-min-scroll-row'><div id='flex-min-scroll-row-side'></div><div id='flex-min-scroll-row-content'>unbreakable-content</div></div>"
        "<div id='flex-min-visible-column'><div id='flex-min-visible-column-side'></div><div id='flex-min-visible-column-content'>X</div></div>"
        "<div id='flex-min-hidden-column'><div id='flex-min-hidden-column-side'></div><div id='flex-min-hidden-column-content'>X</div></div>"
        "<div id='flex-min-auto-column'><div id='flex-min-auto-column-side'></div><div id='flex-min-auto-column-content'>X</div></div>"
        "<div id='flex-min-scroll-column'><div id='flex-min-scroll-column-side'></div><div id='flex-min-scroll-column-content'>X</div></div>"
        "<div id='intrinsic-wrap-shell'><div id='intrinsic-wrap-sidebar'></div><div id='intrinsic-wrap-content'><div id='intrinsic-wrap-grid'><div></div><div></div><div></div><div></div></div></div></div>"
        "<div id='intrinsic-nowrap-shell'><div id='intrinsic-nowrap-sidebar'></div><div id='intrinsic-nowrap-content'><div id='intrinsic-nowrap-grid'><div></div><div></div></div></div></div>"
        "<div id='basis-one'></div><div id='basis-content'></div><div id='basis-none'></div>"
        "<div id='absolute-row-start'><div id='absolute-row-start-child'></div></div>"
        "<div id='absolute-row-center'><div id='absolute-row-center-child'></div></div>"
        "<div id='absolute-row-end'><div id='absolute-row-end-child'></div></div>"
        "<div id='absolute-row-space-between'><div id='absolute-row-space-between-child'></div></div>"
        "<div id='absolute-row-self'><div id='absolute-row-self-child'></div></div>"
        "<div id='absolute-column-end'><div id='absolute-column-end-child'></div></div>"
        "<div id='absolute-insets'><div id='absolute-insets-child'></div></div>"
        "<div id='absolute-overflow'><div id='absolute-overflow-content-box'></div><div id='absolute-overflow-border-box'></div><div id='absolute-fill'></div></div>"
        "<div id='normal-auto-width'><div id='normal-auto-width-child'></div></div>"
        "<div id='static-block'><div id='static-flow'></div><div id='static-absolute'></div></div>"
        "<div id='normal-margin-collapse'><div id='normal-margin-collapse-child'></div></div>"
        "<div id='flex-bfc-shell'><div id='flex-bfc-item'><div id='flex-bfc-child'></div></div></div>"
        "<div id='overflow-bfc-item'><div id='overflow-bfc-child'></div></div>"
        "<div id='positioned-bfc-host'><div id='positioned-bfc-item'><div id='positioned-bfc-child'></div></div></div>"
        "<div id='inline-bfc-host'><div id='inline-bfc-item'><div id='inline-bfc-child'></div></div></div>"
        "<div id='nested-bfc-shell'><div id='nested-bfc-header'><div id='nested-bfc-copy'><div id='nested-bfc-title'>Title</div><p id='nested-bfc-note'>one two three four five six</p></div><div id='nested-bfc-action'></div></div><div id='nested-bfc-next'></div></div>"
        "<div id='reflow-shell'><div id='reflow-header'><div id='reflow-copy'><h1 id='reflow-title'>James Wilson</h1><p id='reflow-note'>Last visit: July 20, 2026 Dr. Aether</p></div><div id='reflow-actions'><button id='reflow-edit'>Edit Profile</button><button id='reflow-new'>New Appointment</button></div></div><div id='reflow-grid'></div></div>"
        "<div id='reflow-root'><div id='reflow-sidebar'></div><div id='reflow-panel'><div id='reflow-panel-header'><input id='reflow-search' placeholder='Search patients...'></div><div id='reflow-patient-list'></div></div><div id='reflow-record'><div id='reflow-real-header'><div id='reflow-real-copy'><h1 id='reflow-real-title'>James Wilson</h1><p id='reflow-real-note'>Last visit: July 20, 2026 Dr. Aether</p></div><div id='reflow-real-actions'><button id='reflow-real-edit'>Edit Profile</button><button id='reflow-real-new'>New Appointment</button></div></div><div id='reflow-real-grid'><div class='reflow-real-card'>Heart Rate</div><div class='reflow-real-card'>Blood Pressure</div><div class='reflow-real-card'>Body Temp</div></div><div id='reflow-real-history'>Medical History</div></div></div>"
        "</body>";
    const char* css =
        "body { display:block; margin:0; box-sizing:border-box; font-size:16px; line-height:1.25; }"
        "p { display:block; margin:1em 0; }"
        "#block { display:block; width:80px; box-sizing:border-box; }"
        "#block-child { display:block; width:50px; height:20px; box-sizing:border-box; margin:0 auto 8px; }"
        "#vertical { display:block; width:80px; height:100px; box-sizing:border-box; }"
        "#vertical-child { display:block; width:20px; height:20px; box-sizing:border-box; margin-top:auto; margin-bottom:auto; }"
        "#row-main { display:flex; width:100px; height:60px; box-sizing:border-box; justify-content:start; align-items:center; }"
        "#row-main-child { display:block; width:20px; height:20px; box-sizing:border-box; margin-left:auto; }"
        "#row-cross { display:flex; width:100px; height:100px; box-sizing:border-box; align-items:end; }"
        "#row-cross-child { display:block; width:20px; height:20px; box-sizing:border-box; margin-top:auto; margin-bottom:auto; }"
        "#column-main { display:flex; flex-direction:column; width:100px; height:100px; box-sizing:border-box; justify-content:start; }"
        "#column-main-child { display:block; width:20px; height:20px; box-sizing:border-box; margin-top:auto; }"
        "#column-cross { display:flex; flex-direction:column; width:100px; height:60px; box-sizing:border-box; align-items:end; }"
        "#column-cross-child { display:block; width:20px; height:20px; box-sizing:border-box; margin-left:auto; margin-right:auto; }"
        "#row-stretch-auto { display:flex; width:160px; box-sizing:border-box; align-items:stretch; }"
        "#row-stretch-short, #row-stretch-tall { width:80px; min-width:0; padding:5px; box-sizing:border-box; }"
        "#row-stretch-fixed { display:flex; width:160px; height:100px; box-sizing:border-box; align-items:stretch; }"
        "#row-stretch-fill, #row-stretch-explicit { width:80px; box-sizing:border-box; }"
        "#row-stretch-explicit { height:30px; }"
        "#row-align-self { display:flex; width:100px; height:100px; box-sizing:border-box; align-items:start; }"
        "#row-align-self-center, #row-align-self-end { width:20px; height:20px; box-sizing:border-box; }"
        "#row-align-self-center { align-self:center; } #row-align-self-end { align-self:end; }"
        "#row-final-main { display:flex; width:100px; box-sizing:border-box; align-items:stretch; }"
        "#row-final-main-short, #row-final-main-wrap { flex:1; min-width:0; padding:5px; box-sizing:border-box; }"
        "#row-stretch-wrap { display:flex; flex-wrap:wrap; width:100px; box-sizing:border-box; align-items:stretch; }"
        "#row-stretch-wrap-a, #row-stretch-wrap-b, #row-stretch-wrap-c { width:50px; flex:none; padding:5px; box-sizing:border-box; }"
        "#column-stretch-auto { display:flex; flex-direction:column; width:160px; height:120px; box-sizing:border-box; align-items:stretch; }"
        "#column-stretch-short, #column-stretch-tall, #column-stretch-explicit { height:20px; box-sizing:border-box; }"
        "#column-stretch-explicit { width:40px; }"
        "#column-stretch-base { display:flex; flex-direction:column; width:80px; box-sizing:border-box; align-items:stretch; }"
        "#column-stretch-base-wrap { height:auto; box-sizing:border-box; }"
        "#column-stretch-base-following { height:10px; box-sizing:border-box; }"
        "#column-natural-base { display:flex; flex-direction:column; width:80px; box-sizing:border-box; align-items:start; }"
        "#column-align-self { display:flex; flex-direction:column; width:100px; height:60px; box-sizing:border-box; align-items:start; }"
        "#column-align-self-center, #column-align-self-end { width:20px; height:20px; box-sizing:border-box; }"
        "#column-align-self-center { align-self:center; } #column-align-self-end { align-self:end; }"
        "#shrink-row, #min-row { display:flex; width:150px; height:20px; box-sizing:border-box; }"
        "#shrink-a { width:100px; height:20px; min-width:0; flex-shrink:1; box-sizing:border-box; }"
        "#shrink-b { width:200px; height:20px; min-width:0; flex-shrink:1; box-sizing:border-box; }"
        "#automatic-row { display:flex; width:150px; height:20px; box-sizing:border-box; }"
        "#automatic-a { width:100px; height:20px; flex-shrink:1; box-sizing:border-box; }"
        "#automatic-b { width:200px; height:20px; flex-shrink:1; box-sizing:border-box; }"
        "#min-a { width:100px; height:20px; min-width:80px; flex-shrink:1; box-sizing:border-box; }"
        "#min-b { width:100px; height:20px; min-width:0; flex-shrink:1; box-sizing:border-box; }"
        "#column-basis { display:flex; flex-direction:column; width:80px; height:200px; box-sizing:border-box; }"
        "#column-fixed { height:50px; flex:none; box-sizing:border-box; }"
        "#column-fill { flex:1; min-height:0; box-sizing:border-box; }"
        "#column-shrink { display:flex; flex-direction:column; width:80px; height:150px; box-sizing:border-box; }"
        "#column-shrink-a { height:100px; flex-shrink:1; box-sizing:border-box; }"
        "#column-shrink-b { height:200px; flex-shrink:1; box-sizing:border-box; }"
        "#wrap-row { display:flex; flex-wrap:wrap; width:100px; box-sizing:border-box; }"
        "#wrap-a { width:60px; max-width:30px; height:10px; flex:none; box-sizing:border-box; }"
        "#wrap-b { width:70px; height:10px; flex:none; box-sizing:border-box; }"
        "#wrap-c { width:40px; height:10px; flex:none; box-sizing:border-box; }"
        "#text-min-row { display:flex; width:150px; box-sizing:border-box; }"
        "#text-min-a, #text-min-b { flex:1; box-sizing:border-box; }"
        "#mixed-inline-min-row { display:flex; width:80px; box-sizing:border-box; }"
        "#mixed-inline-a, #mixed-inline-b { flex:1; box-sizing:border-box; }"
        "#anonymous-flex-tabs { display:flex; width:160px; box-sizing:border-box; }"
        "#anonymous-flex-tab { display:flex; box-sizing:border-box; }"
        "#anonymous-flex-tab-sibling { width:30px; height:1px; flex:none; box-sizing:border-box; }"
        "#opaque-control-row { display:flex; width:300px; box-sizing:border-box; }"
        "#flow-button-row { display:flex; width:70px; box-sizing:border-box; }"
        "#opaque-input { flex:none; padding:10px; box-sizing:border-box; }"
        "#flow-button { flex:1; min-width:0; padding:10px; box-sizing:border-box; }"
        "#multiline-textarea { display:inline-block; flex:none; padding:10px; box-sizing:border-box; }"
        "#overflow-y-axis { overflow-y:auto; }"
        "#overflow-x-axis { overflow-x:hidden; overflow-y:visible; }"
        "#flex-min-visible-row, #flex-min-hidden-row, #flex-min-auto-row, #flex-min-scroll-row { display:flex; width:200px; box-sizing:border-box; }"
        "#flex-min-visible-row-side, #flex-min-hidden-row-side, #flex-min-auto-row-side, #flex-min-scroll-row-side { width:100px; height:10px; flex-shrink:1; box-sizing:border-box; }"
        "#flex-min-visible-row-content, #flex-min-hidden-row-content, #flex-min-auto-row-content, #flex-min-scroll-row-content { flex:1; box-sizing:border-box; }"
        "#flex-min-hidden-row-content { overflow-x:hidden; } #flex-min-auto-row-content { overflow-x:auto; } #flex-min-scroll-row-content { overflow-x:scroll; }"
        "#flex-min-visible-column, #flex-min-hidden-column, #flex-min-auto-column, #flex-min-scroll-column { display:flex; flex-direction:column; width:20px; height:200px; box-sizing:border-box; }"
        "#flex-min-visible-column-side, #flex-min-hidden-column-side, #flex-min-auto-column-side, #flex-min-scroll-column-side { height:100px; flex-shrink:1; box-sizing:border-box; }"
        "#flex-min-visible-column-content, #flex-min-hidden-column-content, #flex-min-auto-column-content, #flex-min-scroll-column-content { flex:1; font-size:200px; line-height:1; box-sizing:border-box; }"
        "#flex-min-hidden-column-content { overflow-y:hidden; } #flex-min-auto-column-content { overflow-y:auto; } #flex-min-scroll-column-content { overflow-y:scroll; }"
        "#intrinsic-wrap-shell, #intrinsic-nowrap-shell { display:flex; width:300px; box-sizing:border-box; }"
        "#intrinsic-wrap-sidebar, #intrinsic-nowrap-sidebar { width:100px; padding:20px; border-right:1px solid black; box-sizing:content-box; }"
        "#intrinsic-wrap-content, #intrinsic-nowrap-content { flex:1; padding:24px; box-sizing:content-box; }"
        "#intrinsic-wrap-grid { display:flex; flex-wrap:wrap; gap:20px; }"
        "#intrinsic-nowrap-grid { display:flex; gap:20px; }"
        "#intrinsic-wrap-grid > div, #intrinsic-nowrap-grid > div { width:50px; height:10px; flex:none; box-sizing:border-box; }"
        "#basis-one { flex:1; }"
        "#basis-content { flex:0 2 content; }"
        "#basis-none { flex:none; }"
        "#absolute-row-start, #absolute-row-center, #absolute-row-end, #absolute-row-space-between, #absolute-row-self, #absolute-column-end, #absolute-insets { position:relative; display:flex; width:200px; height:160px; box-sizing:border-box; }"
        "#absolute-row-start { justify-content:start; align-items:start; }"
        "#absolute-row-center { justify-content:center; align-items:center; }"
        "#absolute-row-end { justify-content:end; align-items:end; }"
        "#absolute-row-space-between { justify-content:space-between; align-items:center; }"
        "#absolute-row-self { justify-content:center; align-items:end; }"
        "#absolute-column-end { flex-direction:column; justify-content:end; align-items:center; }"
        "#absolute-row-start-child, #absolute-row-center-child, #absolute-row-end-child, #absolute-row-space-between-child, #absolute-row-self-child, #absolute-column-end-child, #absolute-insets-child { position:absolute; width:40px; height:20px; box-sizing:border-box; }"
        "#absolute-row-self-child { align-self:center; }"
        "#absolute-insets-child { left:12px; top:8px; }"
        "#absolute-overflow { position:relative; display:block; width:200px; height:200px; box-sizing:border-box; }"
        "#absolute-overflow-content-box { position:absolute; width:180px; height:180px; border:15px solid black; }"
        "#absolute-overflow-border-box { position:absolute; width:180px; height:180px; border:15px solid black; box-sizing:border-box; }"
        "#absolute-fill { position:absolute; left:10px; right:10px; top:10px; bottom:10px; border:5px solid black; box-sizing:border-box; }"
        "#normal-auto-width { display:block; width:200px; box-sizing:border-box; }"
        "#normal-auto-width-child { display:block; height:10px; box-sizing:border-box; }"
        "#static-block { position:relative; display:block; width:200px; height:80px; box-sizing:border-box; }"
        "#static-flow { display:block; width:50px; height:20px; box-sizing:border-box; }"
        "#static-absolute { position:absolute; width:40px; height:20px; box-sizing:border-box; }"
        "#normal-margin-collapse, #flex-bfc-shell, #overflow-bfc-item, #positioned-bfc-host, #inline-bfc-host { display:block; width:100px; box-sizing:border-box; }"
        "#normal-margin-collapse-child, #flex-bfc-child, #overflow-bfc-child, #positioned-bfc-child, #inline-bfc-child { display:block; height:10px; margin-bottom:20px; box-sizing:border-box; }"
        "#flex-bfc-shell { display:flex; flex-direction:column; }"
        "#flex-bfc-item { display:block; }"
        "#overflow-bfc-item { overflow:hidden; }"
        "#positioned-bfc-host { position:relative; height:80px; }"
        "#positioned-bfc-item { position:absolute; width:100px; }"
        "#inline-bfc-item { display:inline-block; width:100px; }"
        "#nested-bfc-shell { display:flex; flex-direction:column; width:100px; gap:30px; box-sizing:border-box; }"
        "#nested-bfc-header { display:flex; align-items:start; justify-content:space-between; box-sizing:border-box; }"
        "#nested-bfc-copy { display:block; box-sizing:border-box; }"
        "#nested-bfc-title { display:block; height:10px; box-sizing:border-box; }"
        "#nested-bfc-note { display:block; margin:5px 0 16px; box-sizing:border-box; }"
        "#nested-bfc-action { display:block; flex:none; width:30px; height:10px; box-sizing:border-box; }"
        "#nested-bfc-next { display:block; height:10px; box-sizing:border-box; }"
        "#reflow-shell { display:flex; flex-direction:column; width:300px; gap:30px; box-sizing:border-box; }"
        "#reflow-header { display:flex; justify-content:space-between; align-items:start; box-sizing:border-box; }"
        "#reflow-copy { display:block; box-sizing:border-box; }"
        "#reflow-title { display:block; font-size:32px; font-weight:800; margin:0; box-sizing:border-box; }"
        "#reflow-note { display:block; margin-top:5px; box-sizing:border-box; }"
        "#reflow-actions { display:flex; gap:10px; box-sizing:border-box; }"
        "#reflow-edit, #reflow-new { padding:12px 20px; min-width:0; box-sizing:border-box; }"
        "#reflow-grid { display:block; height:20px; box-sizing:border-box; }"
        "#reflow-root { display:flex; width:770px; height:600px; box-sizing:border-box; }"
        "#reflow-sidebar { width:100px; box-sizing:border-box; }"
        "#reflow-panel { width:350px; display:flex; flex-direction:column; box-sizing:border-box; }"
        "#reflow-panel-header { padding:30px; box-sizing:border-box; }"
        "#reflow-search { width:100%; padding:12px 15px; box-sizing:border-box; }"
        "#reflow-patient-list { flex:1; overflow-y:auto; }"
        "#reflow-record { flex:1; padding:40px; overflow-y:auto; display:flex; flex-direction:column; gap:30px; }"
        "#reflow-real-header { display:flex; justify-content:space-between; align-items:start; box-sizing:border-box; }"
        "#reflow-real-copy { display:block; box-sizing:border-box; }"
        "#reflow-real-title { display:block; font-size:32px; font-weight:800; margin:0; box-sizing:border-box; }"
        "#reflow-real-note { display:block; margin-top:5px; box-sizing:border-box; }"
        "#reflow-real-actions { display:flex; gap:10px; box-sizing:border-box; }"
        "#reflow-real-edit, #reflow-real-new { padding:12px 20px; box-sizing:border-box; }"
        "#reflow-real-grid { display:flex; gap:20px; box-sizing:border-box; }"
        ".reflow-real-card { flex:1; padding:25px; box-sizing:border-box; }"
        "#reflow-real-history { flex:1; padding:30px; box-sizing:border-box; }";

    aether_doc_t doc = nullptr;
    require(aether_doc_create(&doc) == AETHER_OK && aether_doc_parse_html(doc, markup) == AETHER_OK,
        "fixture document creation failed");
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{300.f, 600.f, 1.f, 16.f, 0, 0, 0};
    aether_style_runtime_t runtime = nullptr;
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
        "style runtime creation failed");
    int64_t stylesheet = 0;
    int32_t rule_count = 0;
    require(aether_style_runtime_add_stylesheet(runtime, css, 0, "auto-margins.css", &stylesheet, &rule_count) == AETHER_OK,
        "stylesheet creation failed");
    aether_style_update_t* updates = nullptr;
    const int32_t update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    require(update_count > 0, "style resolution failed");
    const auto basis_one_node = aether_doc_get_element_by_id(doc, "basis-one");
    const auto basis_content_node = aether_doc_get_element_by_id(doc, "basis-content");
    const auto basis_none_node = aether_doc_get_element_by_id(doc, "basis-none");
    const auto overflow_y_axis_node = aether_doc_get_element_by_id(doc, "overflow-y-axis");
    const auto overflow_x_axis_node = aether_doc_get_element_by_id(doc, "overflow-x-axis");
    require(basis_one_node && basis_content_node && basis_none_node && overflow_y_axis_node && overflow_x_axis_node,
        "flex-basis and overflow fixture lookup failed");
    aether_computed_style_t basis_one{};
    aether_computed_style_t basis_content{};
    aether_computed_style_t basis_none{};
    aether_computed_style_t overflow_y_axis{};
    aether_computed_style_t overflow_x_axis{};
    bool found_basis_one = false, found_basis_content = false, found_basis_none = false;
    bool found_overflow_y_axis = false, found_overflow_x_axis = false;
    for (int32_t index = 0; index < update_count; ++index) {
        aether_node_t node = nullptr;
        aether_computed_style_handle_t handle = nullptr;
        require(aether_style_runtime_get_update(updates[index], &node, &handle, nullptr) == AETHER_OK,
            "style update access failed");
        if (node == basis_one_node) {
            require(aether_computed_style_copy(handle, &basis_one) == AETHER_OK, "flex: 1 style copy failed");
            found_basis_one = true;
        } else if (node == basis_content_node) {
            require(aether_computed_style_copy(handle, &basis_content) == AETHER_OK, "content basis style copy failed");
            found_basis_content = true;
        } else if (node == basis_none_node) {
            require(aether_computed_style_copy(handle, &basis_none) == AETHER_OK, "flex: none style copy failed");
            found_basis_none = true;
        } else if (node == overflow_y_axis_node) {
            require(aether_computed_style_copy(handle, &overflow_y_axis) == AETHER_OK, "overflow-y style copy failed");
            found_overflow_y_axis = true;
        } else if (node == overflow_x_axis_node) {
            require(aether_computed_style_copy(handle, &overflow_x_axis) == AETHER_OK, "overflow-x style copy failed");
            found_overflow_x_axis = true;
        }
        aether_computed_style_handle_destroy(handle);
    }
    require(found_basis_one && found_basis_content && found_basis_none &&
            found_overflow_y_axis && found_overflow_x_axis,
        "flex-basis and overflow fixture styles were not resolved");
    aether_style_updates_free(updates, update_count);
    aether_style_map_t styles = nullptr;
    require(aether_style_runtime_create_resolved_style_map(runtime, doc, &styles) == AETHER_OK,
        "resolved style-map creation failed");
    aether_layout_engine_t engine = nullptr;
    require(aether_layout_create(measure_empty, nullptr, &engine) == AETHER_OK,
        "layout engine creation failed");
    aether_layout_result_t result = nullptr;
    require(aether_layout_layout(engine, doc, styles, 300.f, 600.f, nullptr, nullptr, &result) == AETHER_OK,
        "layout failed");

    const auto block = box_for(doc, result, "block");
    const auto block_child = box_for(doc, result, "block-child");
    const auto block_content = aether_layout_box_get_content_box(block);
    const auto block_child_margin = aether_layout_box_get_margin_box(block_child);
    const auto block_child_border = aether_layout_box_get_border_box(block_child);
    require(same(block_child_margin.width, 80.f) && same(block_child_border.x, block_content.x + 15.f),
        "a definite block width with two horizontal auto margins must center its child");

    const auto vertical = box_for(doc, result, "vertical");
    const auto vertical_child = box_for(doc, result, "vertical-child");
    require(same(aether_layout_box_get_border_box(vertical_child).y, aether_layout_box_get_content_box(vertical).y),
        "vertical auto margins in normal block flow must resolve to zero");

    const auto row_main = box_for(doc, result, "row-main");
    const auto row_main_child = box_for(doc, result, "row-main-child");
    require(same(aether_layout_box_get_border_box(row_main_child).x,
                 aether_layout_box_get_content_box(row_main).x + 80.f),
        "a row flex-item main-axis auto margin must consume free space before justify-content");

    const auto row_cross = box_for(doc, result, "row-cross");
    const auto row_cross_child = box_for(doc, result, "row-cross-child");
    require(same(aether_layout_box_get_border_box(row_cross_child).y,
                 aether_layout_box_get_content_box(row_cross).y + 40.f),
        "row flex cross-axis auto margins must override align-items using available cross space");

    const auto column_main = box_for(doc, result, "column-main");
    const auto column_main_child = box_for(doc, result, "column-main-child");
    require(same(aether_layout_box_get_border_box(column_main_child).y,
                 aether_layout_box_get_content_box(column_main).y + 80.f),
        "a column flex-item main-axis auto margin must consume free space before justify-content");

    const auto column_cross = box_for(doc, result, "column-cross");
    const auto column_cross_child = box_for(doc, result, "column-cross-child");
    require(same(aether_layout_box_get_border_box(column_cross_child).x,
                 aether_layout_box_get_content_box(column_cross).x + 40.f),
        "column flex cross-axis auto margins must override align-items using available cross space");

    const auto row_stretch_short = box_for(doc, result, "row-stretch-short");
    const auto row_stretch_tall = box_for(doc, result, "row-stretch-tall");
    require(same(aether_layout_box_get_border_box(row_stretch_short).height,
                 aether_layout_box_get_border_box(row_stretch_tall).height) &&
            aether_layout_box_get_border_box(row_stretch_tall).height > 20.f,
        "an auto-height row flex item must stretch to its line's final wrapped cross size");

    const auto row_stretch_fill = box_for(doc, result, "row-stretch-fill");
    const auto row_stretch_explicit = box_for(doc, result, "row-stretch-explicit");
    require(same(aether_layout_box_get_border_box(row_stretch_fill).height, 100.f) &&
            same(aether_layout_box_get_border_box(row_stretch_explicit).height, 30.f),
        "cross-axis stretch must fill a definite line only for auto-sized row items");

    const auto row_align_self = box_for(doc, result, "row-align-self");
    const auto row_align_self_center = box_for(doc, result, "row-align-self-center");
    const auto row_align_self_end = box_for(doc, result, "row-align-self-end");
    require(same(aether_layout_box_get_border_box(row_align_self_center).y,
                 aether_layout_box_get_content_box(row_align_self).y + 40.f) &&
            same(aether_layout_box_get_border_box(row_align_self_end).y,
                 aether_layout_box_get_content_box(row_align_self).y + 80.f),
        "align-self must override align-items for normal row flex items on the cross axis");

    const auto row_final_main_short = box_for(doc, result, "row-final-main-short");
    const auto row_final_main_wrap = box_for(doc, result, "row-final-main-wrap");
    require(same(aether_layout_box_get_border_box(row_final_main_short).height,
                 aether_layout_box_get_border_box(row_final_main_wrap).height) &&
            aether_layout_box_get_border_box(row_final_main_wrap).height > 20.f,
        "cross-size determination must use the final shrunken main size that causes wrapping");

    const auto row_stretch_wrap_a = box_for(doc, result, "row-stretch-wrap-a");
    const auto row_stretch_wrap_b = box_for(doc, result, "row-stretch-wrap-b");
    const auto row_stretch_wrap_c = box_for(doc, result, "row-stretch-wrap-c");
    require(same(aether_layout_box_get_border_box(row_stretch_wrap_a).height,
                 aether_layout_box_get_border_box(row_stretch_wrap_b).height) &&
            aether_layout_box_get_border_box(row_stretch_wrap_c).y >
                aether_layout_box_get_border_box(row_stretch_wrap_a).y,
        "wrapped row flex lines must stretch only within their own computed cross extent");

    const auto column_stretch_auto = box_for(doc, result, "column-stretch-auto");
    const auto column_stretch_short = box_for(doc, result, "column-stretch-short");
    const auto column_stretch_tall = box_for(doc, result, "column-stretch-tall");
    const auto column_stretch_explicit = box_for(doc, result, "column-stretch-explicit");
    const auto column_stretch_content = aether_layout_box_get_content_box(column_stretch_auto);
    require(same(aether_layout_box_get_border_box(column_stretch_short).width, column_stretch_content.width) &&
            same(aether_layout_box_get_border_box(column_stretch_tall).width, column_stretch_content.width) &&
            same(aether_layout_box_get_border_box(column_stretch_explicit).width, 40.f),
        "cross-axis stretch must fill a column's definite width only for auto-sized items");

    const auto column_stretch_base_wrap = box_for(doc, result, "column-stretch-base-wrap");
    const auto column_stretch_base_following = box_for(doc, result, "column-stretch-base-following");
    require(aether_layout_box_get_border_box(column_stretch_base_wrap).height > 1.f &&
            aether_layout_box_get_border_box(column_stretch_base_following).y >=
                aether_layout_box_get_border_box(column_stretch_base_wrap).y +
                aether_layout_box_get_border_box(column_stretch_base_wrap).height,
        "single-line column stretch must measure its main flex base at the definite stretched width before text wrapping");

    const auto column_natural_base_content = box_for(doc, result, "column-natural-base-content");
    require(aether_layout_box_get_border_box(column_natural_base_content).width > 80.f,
        "non-stretched column items must retain their natural cross-size during flex-base measurement");

    const auto column_align_self = box_for(doc, result, "column-align-self");
    const auto column_align_self_center = box_for(doc, result, "column-align-self-center");
    const auto column_align_self_end = box_for(doc, result, "column-align-self-end");
    require(same(aether_layout_box_get_border_box(column_align_self_center).x,
                 aether_layout_box_get_content_box(column_align_self).x + 40.f) &&
            same(aether_layout_box_get_border_box(column_align_self_end).x,
                 aether_layout_box_get_content_box(column_align_self).x + 80.f),
        "align-self must override align-items for normal column flex items on the cross axis");

    const auto shrink_a = box_for(doc, result, "shrink-a");
    const auto shrink_b = box_for(doc, result, "shrink-b");
    require(same(aether_layout_box_get_border_box(shrink_a).width, 50.f) &&
            same(aether_layout_box_get_border_box(shrink_b).width, 100.f),
        "row flex shrink must be weighted by each item flex base size");

    const auto automatic_a = box_for(doc, result, "automatic-a");
    const auto automatic_b = box_for(doc, result, "automatic-b");
    require(same(aether_layout_box_get_border_box(automatic_a).width, 50.f) &&
            same(aether_layout_box_get_border_box(automatic_b).width, 100.f),
        "the automatic flex minimum must use content size rather than the specified flex base");

    const auto min_a = box_for(doc, result, "min-a");
    const auto min_b = box_for(doc, result, "min-b");
    require(same(aether_layout_box_get_border_box(min_a).width, 80.f) &&
            same(aether_layout_box_get_border_box(min_b).width, 70.f),
        "a flex-item minimum must freeze only that item and redistribute the remaining shrinkage");

    const auto column_fixed = box_for(doc, result, "column-fixed");
    const auto column_fill = box_for(doc, result, "column-fill");
    require(same(aether_layout_box_get_border_box(column_fixed).height, 50.f) &&
            same(aether_layout_box_get_border_box(column_fill).height, 150.f),
        "column flex must consume the same resolved flex-basis output as rows");

    const auto column_shrink_a = box_for(doc, result, "column-shrink-a");
    const auto column_shrink_b = box_for(doc, result, "column-shrink-b");
    require(same(aether_layout_box_get_border_box(column_shrink_a).height, 50.f) &&
            same(aether_layout_box_get_border_box(column_shrink_b).height, 100.f),
        "column flex must apply the same automatic minimum and weighted shrink semantics as rows");

    const auto wrap_a = box_for(doc, result, "wrap-a");
    const auto wrap_b = box_for(doc, result, "wrap-b");
    const auto wrap_c = box_for(doc, result, "wrap-c");
    require(same(aether_layout_box_get_border_box(wrap_a).y, aether_layout_box_get_border_box(wrap_b).y) &&
            aether_layout_box_get_border_box(wrap_c).y > aether_layout_box_get_border_box(wrap_a).y,
        "wrap must form lines from hypothetical sizes before flex resolution");

    const auto text_min_a = box_for(doc, result, "text-min-a");
    const auto text_min_b = box_for(doc, result, "text-min-b");
    require(same(aether_layout_box_get_border_box(text_min_a).width, 80.f) &&
            same(aether_layout_box_get_border_box(text_min_b).width, 70.f),
        "automatic flex minimums must use each text run's widest whitespace-delimited fragment");

    const auto mixed_inline_a = box_for(doc, result, "mixed-inline-a");
    const auto mixed_inline_b = box_for(doc, result, "mixed-inline-b");
    require(same(aether_layout_box_get_border_box(mixed_inline_a).width, 80.f) &&
            same(aether_layout_box_get_border_box(mixed_inline_b).width, 10.f),
        "mixed text and inline descendants must retain the direct text sibling in automatic flex minimum sizing");

    const auto anonymous_flex_tab = box_for(doc, result, "anonymous-flex-tab");
    const auto anonymous_flex_tab_sibling = box_for(doc, result, "anonymous-flex-tab-sibling");
    require(same(aether_layout_box_get_border_box(anonymous_flex_tab).width, 100.f) &&
            same(aether_layout_box_get_border_box(anonymous_flex_tab_sibling).x,
                 aether_layout_box_get_border_box(anonymous_flex_tab).x + 100.f),
        "a flex container's intrinsic sizing must consume the same anonymous direct-text item as final layout");

    const auto opaque_input = box_for(doc, result, "opaque-input");
    const auto flow_button = box_for(doc, result, "flow-button");
    const auto multiline_textarea = box_for(doc, result, "multiline-textarea");
    require(aether_layout_box_is_atomic(opaque_input) &&
            !aether_layout_box_is_atomic(flow_button) &&
            !aether_layout_box_is_atomic(multiline_textarea) &&
            same(aether_layout_box_get_border_box(flow_button).width, 70.f),
        "the central form-control table must keep input opaque while routing button and multiline textarea away from atomic layout");

    const auto intrinsic_wrap_sidebar = box_for(doc, result, "intrinsic-wrap-sidebar");
    const auto intrinsic_wrap_content = box_for(doc, result, "intrinsic-wrap-content");
    const auto intrinsic_nowrap_content = box_for(doc, result, "intrinsic-nowrap-content");
    require(same(aether_layout_box_get_border_box(intrinsic_wrap_sidebar).width, 141.f) &&
            same(aether_layout_box_get_border_box(intrinsic_wrap_content).width, 159.f),
        "a wrapped row flex container must not inflate a sibling's automatic flex minimum by summing every item");
    require(aether_layout_box_get_border_box(intrinsic_nowrap_content).width >= 168.f,
        "a single-line row flex container must retain the summed item-and-gap min-content contribution");

    require(same(basis_one.flex_grow, 1.f) && same(basis_one.flex_shrink, 1.f) &&
            basis_one.flex_basis.kind == AETHER_FLEX_BASIS_LENGTH &&
            basis_one.flex_basis.length.unit == AETHER_CSS_UNIT_PERCENT && same(basis_one.flex_basis.length.value, 0.f),
        "flex: 1 must preserve its grow, shrink, and 0% basis semantics in computed style");
    aether_computed_style_free(&basis_one);

    require(same(basis_content.flex_grow, 0.f) && same(basis_content.flex_shrink, 2.f) &&
            basis_content.flex_basis.kind == AETHER_FLEX_BASIS_CONTENT,
        "flex shorthand must preserve the content flex-basis keyword");
    aether_computed_style_free(&basis_content);

    require(same(basis_none.flex_grow, 0.f) && same(basis_none.flex_shrink, 0.f) &&
            basis_none.flex_basis.kind == AETHER_FLEX_BASIS_AUTO,
        "flex: none must preserve its auto basis and zero flexibility");
    aether_computed_style_free(&basis_none);

    require(overflow_y_axis.overflow_y == AETHER_OVERFLOW_AUTO &&
            overflow_y_axis.overflow_x == AETHER_OVERFLOW_AUTO,
        "overflow-y:auto must create scroll-container semantics without clipping the declaration's other axis silently");
    require(overflow_x_axis.overflow_x == AETHER_OVERFLOW_HIDDEN &&
            overflow_x_axis.overflow_y == AETHER_OVERFLOW_AUTO,
        "overflow-x must remain independent and normalize visible overflow-y per CSS Overflow");
    aether_computed_style_free(&overflow_y_axis);
    aether_computed_style_free(&overflow_x_axis);

    const auto visible_row_side = box_for(doc, result, "flex-min-visible-row-side");
    const auto hidden_row_side = box_for(doc, result, "flex-min-hidden-row-side");
    const auto auto_row_side = box_for(doc, result, "flex-min-auto-row-side");
    const auto scroll_row_side = box_for(doc, result, "flex-min-scroll-row-side");
    require(aether_layout_box_get_border_box(visible_row_side).width < 100.f - .001f,
        "visible overflow must retain a row flex item's content-based automatic minimum");
    require(same(aether_layout_box_get_border_box(hidden_row_side).width, 100.f) &&
            same(aether_layout_box_get_border_box(auto_row_side).width, 100.f) &&
            same(aether_layout_box_get_border_box(scroll_row_side).width, 100.f),
        "hidden, auto, and scroll overflow must zero a row flex item's automatic minimum");

    const auto visible_column_side = box_for(doc, result, "flex-min-visible-column-side");
    const auto hidden_column_side = box_for(doc, result, "flex-min-hidden-column-side");
    const auto auto_column_side = box_for(doc, result, "flex-min-auto-column-side");
    const auto scroll_column_side = box_for(doc, result, "flex-min-scroll-column-side");
    require(aether_layout_box_get_border_box(visible_column_side).height < 100.f - .001f,
        "visible overflow must retain a column flex item's content-based automatic minimum");
    require(aether_layout_box_get_border_box(hidden_column_side).height >
                aether_layout_box_get_border_box(visible_column_side).height + .001f &&
            same(aether_layout_box_get_border_box(hidden_column_side).height,
                 aether_layout_box_get_border_box(auto_column_side).height) &&
            same(aether_layout_box_get_border_box(hidden_column_side).height,
                 aether_layout_box_get_border_box(scroll_column_side).height),
        "hidden, auto, and scroll overflow must zero a column flex item's automatic minimum");

    const auto row_start = box_for(doc, result, "absolute-row-start");
    const auto row_start_child = box_for(doc, result, "absolute-row-start-child");
    require(same(aether_layout_box_get_border_box(row_start_child).x, aether_layout_box_get_content_box(row_start).x) &&
            same(aether_layout_box_get_border_box(row_start_child).y, aether_layout_box_get_content_box(row_start).y),
        "an absolute flex child must use start/start static positioning independently");

    const auto row_center = box_for(doc, result, "absolute-row-center");
    const auto row_center_child = box_for(doc, result, "absolute-row-center-child");
    require(same(aether_layout_box_get_border_box(row_center_child).x, aether_layout_box_get_content_box(row_center).x + 80.f) &&
            same(aether_layout_box_get_border_box(row_center_child).y, aether_layout_box_get_content_box(row_center).y + 70.f),
        "an absolute flex child must use center/center static positioning without sibling drift");

    const auto row_end = box_for(doc, result, "absolute-row-end");
    const auto row_end_child = box_for(doc, result, "absolute-row-end-child");
    require(same(aether_layout_box_get_border_box(row_end_child).x, aether_layout_box_get_content_box(row_end).x + 160.f) &&
            same(aether_layout_box_get_border_box(row_end_child).y, aether_layout_box_get_content_box(row_end).y + 140.f),
        "an absolute flex child must use end/end static positioning independently");

    const auto row_space_between = box_for(doc, result, "absolute-row-space-between");
    const auto row_space_between_child = box_for(doc, result, "absolute-row-space-between-child");
    require(same(aether_layout_box_get_border_box(row_space_between_child).x,
                 aether_layout_box_get_content_box(row_space_between).x) &&
            same(aether_layout_box_get_border_box(row_space_between_child).y,
                 aether_layout_box_get_content_box(row_space_between).y + 70.f),
        "space-between must reduce to start for the sole hypothetical absolute flex child");

    const auto row_self = box_for(doc, result, "absolute-row-self");
    const auto row_self_child = box_for(doc, result, "absolute-row-self-child");
    require(same(aether_layout_box_get_border_box(row_self_child).x, aether_layout_box_get_content_box(row_self).x + 80.f) &&
            same(aether_layout_box_get_border_box(row_self_child).y, aether_layout_box_get_content_box(row_self).y + 70.f),
        "align-self must override align-items for an absolute flex child's static cross position");

    const auto column_end = box_for(doc, result, "absolute-column-end");
    const auto column_end_child = box_for(doc, result, "absolute-column-end-child");
    require(same(aether_layout_box_get_border_box(column_end_child).x, aether_layout_box_get_content_box(column_end).x + 80.f) &&
            same(aether_layout_box_get_border_box(column_end_child).y, aether_layout_box_get_content_box(column_end).y + 140.f),
        "column flex static positioning must map justify-content to its vertical main axis");

    const auto insets = box_for(doc, result, "absolute-insets");
    const auto insets_child = box_for(doc, result, "absolute-insets-child");
    require(same(aether_layout_box_get_border_box(insets_child).x, aether_layout_box_get_content_box(insets).x + 12.f) &&
            same(aether_layout_box_get_border_box(insets_child).y, aether_layout_box_get_content_box(insets).y + 8.f),
        "explicit absolute insets must override a flex static position");

    const auto absolute_overflow = box_for(doc, result, "absolute-overflow");
    const auto absolute_content_box = box_for(doc, result, "absolute-overflow-content-box");
    const auto absolute_border_box = box_for(doc, result, "absolute-overflow-border-box");
    const auto absolute_fill = box_for(doc, result, "absolute-fill");
    const auto overflow_content = aether_layout_box_get_content_box(absolute_overflow);
    const auto content_box_rect = aether_layout_box_get_border_box(absolute_content_box);
    const auto border_box_rect = aether_layout_box_get_border_box(absolute_border_box);
    const auto fill_rect = aether_layout_box_get_border_box(absolute_fill);
    require(same(content_box_rect.width, 210.f) && same(content_box_rect.height, 210.f) &&
            content_box_rect.width > overflow_content.width && content_box_rect.height > overflow_content.height,
        "a definite content-box size on an absolute child must retain its decorations and may overflow both axes");
    require(same(border_box_rect.width, 180.f) && same(border_box_rect.height, 180.f),
        "box-sizing:border-box must retain a definite absolute border-box size");
    require(same(fill_rect.width, 180.f) && same(fill_rect.height, 180.f),
        "opposing absolute insets must continue to force the available border-box size");

    const auto normal_auto_width = box_for(doc, result, "normal-auto-width");
    const auto normal_auto_width_child = box_for(doc, result, "normal-auto-width-child");
    require(same(aether_layout_box_get_border_box(normal_auto_width_child).width,
                 aether_layout_box_get_content_box(normal_auto_width).width),
        "an auto-sized normal-flow block must continue to resolve to its available width");

    const auto static_flow = box_for(doc, result, "static-flow");
    const auto static_absolute = box_for(doc, result, "static-absolute");
    require(same(aether_layout_box_get_border_box(static_absolute).x,
                 aether_layout_box_get_margin_box(static_flow).x + aether_layout_box_get_margin_box(static_flow).width),
        "the legacy block static-position path must remain unchanged");

    const auto normal_margin_collapse = box_for(doc, result, "normal-margin-collapse");
    const auto flex_bfc_item = box_for(doc, result, "flex-bfc-item");
    const auto overflow_bfc_item = box_for(doc, result, "overflow-bfc-item");
    const auto positioned_bfc_item = box_for(doc, result, "positioned-bfc-item");
    const auto inline_bfc_item = box_for(doc, result, "inline-bfc-item");
    require(same(aether_layout_box_get_border_box(normal_margin_collapse).height, 10.f),
        "a normal block must retain parent-end margin collapse when it does not establish a BFC");
    require(same(aether_layout_box_get_border_box(flex_bfc_item).height, 30.f),
        "a flex item must establish a BFC and contain its final block child's margin");
    require(same(aether_layout_box_get_border_box(overflow_bfc_item).height, 30.f),
        "an overflow BFC must continue to contain its final block child's margin");
    require(same(aether_layout_box_get_border_box(positioned_bfc_item).height, 30.f),
        "a positioned box must establish a BFC and contain its final block child's margin");
    require(same(aether_layout_box_get_border_box(inline_bfc_item).height, 30.f),
        "an inline-block atomic layout item must establish a BFC and contain its final block child's margin");

    const auto nested_bfc_header = box_for(doc, result, "nested-bfc-header");
    const auto nested_bfc_copy = box_for(doc, result, "nested-bfc-copy");
    const auto nested_bfc_note = box_for(doc, result, "nested-bfc-note");
    const auto nested_bfc_next = box_for(doc, result, "nested-bfc-next");
    const auto nested_header_border = aether_layout_box_get_border_box(nested_bfc_header);
    const auto nested_copy_border = aether_layout_box_get_border_box(nested_bfc_copy);
    const auto nested_note_border = aether_layout_box_get_border_box(nested_bfc_note);
    const auto nested_next_border = aether_layout_box_get_border_box(nested_bfc_next);
    require(nested_header_border.y + nested_header_border.height >= nested_copy_border.y + nested_copy_border.height &&
            nested_copy_border.y + nested_copy_border.height >= nested_note_border.y + nested_note_border.height,
        "a nested flex row must publish a cross-size that contains its reflowed multiline block descendant");
    require(nested_next_border.y >= nested_header_border.y + nested_header_border.height + 30.f,
        "a column flex gap must begin after a nested flex header's final reflowed border box");

    const auto reflow_header = box_for(doc, result, "reflow-header");
    const auto reflow_copy = box_for(doc, result, "reflow-copy");
    const auto reflow_note = box_for(doc, result, "reflow-note");
    const auto reflow_grid = box_for(doc, result, "reflow-grid");
    const auto reflow_header_border = aether_layout_box_get_border_box(reflow_header);
    const auto reflow_copy_border = aether_layout_box_get_border_box(reflow_copy);
    const auto reflow_note_border = aether_layout_box_get_border_box(reflow_note);
    const auto reflow_grid_border = aether_layout_box_get_border_box(reflow_grid);
    require(reflow_header_border.y + reflow_header_border.height >=
                reflow_copy_border.y + reflow_copy_border.height &&
            reflow_copy_border.y + reflow_copy_border.height >=
                reflow_note_border.y + reflow_note_border.height,
        "a flex header must publish the final wrapped extent of a normal-flow copy block beside buttons");
    require(reflow_grid_border.y >= reflow_header_border.y + reflow_header_border.height + 30.f,
        "a column gap must begin after the final reflowed flex-header extent beside buttons");

    const auto reflow_real_header = box_for(doc, result, "reflow-real-header");
    const auto reflow_real_copy = box_for(doc, result, "reflow-real-copy");
    const auto reflow_real_note = box_for(doc, result, "reflow-real-note");
    const auto reflow_real_grid = box_for(doc, result, "reflow-real-grid");
    const auto reflow_real_header_border = aether_layout_box_get_border_box(reflow_real_header);
    const auto reflow_real_copy_border = aether_layout_box_get_border_box(reflow_real_copy);
    const auto reflow_real_note_border = aether_layout_box_get_border_box(reflow_real_note);
    const auto reflow_real_grid_border = aether_layout_box_get_border_box(reflow_real_grid);
    require(reflow_real_header_border.y + reflow_real_header_border.height >=
                reflow_real_copy_border.y + reflow_real_copy_border.height &&
            reflow_real_copy_border.y + reflow_real_copy_border.height >=
                reflow_real_note_border.y + reflow_real_note_border.height,
        "a flexed record area must retain its nested header's final multiline copy extent after the outer row resolves its width");
    require(reflow_real_grid_border.y >= reflow_real_header_border.y + reflow_real_header_border.height + 30.f,
        "a flexed record area's column gap must start after a header reflowed at the outer row's final width");

    auto* internal_result = static_cast<aether_layout_result_s*>(result);
    require(internal_result->live_box_count ==
                static_cast<size_t>(internal_result->root->descendants_and_self()),
        "flex base-measurement boxes must be released instead of surviving outside the final layout tree");

    aether_layout_result_destroy(result);
    aether_layout_destroy(engine);
    aether_style_map_destroy(styles);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(doc);
    return 0;
}
