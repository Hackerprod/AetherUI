#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <aether/aether_contextual_transitions.h>
#include <aether/aether_contextual_layout.h>
#include "../src/css/style_map_types.h"
#include "../src/css/style_revision.h"
#include "../src/layout/style_lookup.h"
#include "../src/dom/dom_types.h"
#include "../src/css/style_handle_export.h"
#include "../src/css/computed_style_handle.h"
#include "../src/layout/layout_types.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <new>
#include <vector>
#include <string>
#include <limits>
#include <execinfo.h>
#include <typeinfo>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"sampler check line %d: %s\n",__LINE__,#x); std::abort(); } } while(0)
namespace {
bool region=false;
// The compiler does not know link-time --wrap can observe this flag through
// builtin malloc. Volatile keeps the bracket around operator new in Release.
volatile bool inside_new=false;
int family=-1, fail_at=-1;
size_t calls[4]{};
size_t exports=0, gets=0, clones=0, destroys=0;
void* failure_stack[32]{}; int failure_depth=0;
bool allocation_fails(int f) {
    if (!region || (f != 0 && inside_new)) return false;
    const size_t ordinal=calls[f]++;
    if (family!=f || fail_at<0 || ordinal!=size_t(fail_at)) return false;
    // Warmed before instrumentation. No dynamic storage in our trace ledger.
    const bool previous=region; region=false;
    failure_depth=backtrace(failure_stack,32); region=previous;
    return true;
}
void reset(int f=-1,int ordinal=-1) {
    family=f; fail_at=ordinal; failure_depth=0;
    for(auto& n:calls)n=0;
    exports=gets=clones=destroys=0;
}
}
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t,size_t);
extern "C" void* __real_realloc(void*,size_t);
extern "C" void* __wrap_malloc(size_t n) { return allocation_fails(1)?nullptr:__real_malloc(n); }
extern "C" void* __wrap_calloc(size_t n,size_t s) { return allocation_fails(2)?nullptr:__real_calloc(n,s); }
extern "C" void* __wrap_realloc(void* p,size_t n) { return allocation_fails(3)?nullptr:__real_realloc(p,n); }
void* operator new(size_t n) {
    if(allocation_fails(0))throw std::bad_alloc();
    const bool previous=inside_new; inside_new=true;
    void* p=std::malloc(n?n:1); inside_new=previous;
    if(!p)throw std::bad_alloc(); return p;
}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete[](void* p,size_t) noexcept {std::free(p);}
void* operator new(size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept {std::free(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {std::free(p);}

using aether::style::MapVersion;
using aether::style::FrozenNodeRef;
extern "C" aether_status_t real_export(const MapVersion&,const FrozenNodeRef&,aether_computed_style_handle_t*)
 asm("__real__ZN6aether5style19export_style_handleERKNS0_10MapVersionERKSt10shared_ptrIKNS0_16FrozenNodeRecordEEPP30aether_computed_style_handle_s");
extern "C" aether_status_t wrap_export(const MapVersion&,const FrozenNodeRef&,aether_computed_style_handle_t*)
 asm("__wrap__ZN6aether5style19export_style_handleERKNS0_10MapVersionERKSt10shared_ptrIKNS0_16FrozenNodeRecordEEPP30aether_computed_style_handle_s");
extern "C" aether_status_t wrap_export(const MapVersion& v,const FrozenNodeRef& r,aether_computed_style_handle_t* out){if(region)++exports;return real_export(v,r,out);}
extern "C" aether_computed_style_handle_t __real_aether_style_map_get(aether_style_map_t,aether_node_t);
extern "C" aether_computed_style_handle_t __wrap_aether_style_map_get(aether_style_map_t m,aether_node_t n){if(region)++gets;return __real_aether_style_map_get(m,n);}
extern "C" void __real_aether_computed_style_handle_destroy(aether_computed_style_handle_t);
extern "C" void __wrap_aether_computed_style_handle_destroy(aether_computed_style_handle_t h){if(region)++destroys;__real_aether_computed_style_handle_destroy(h);}
extern "C" aether_status_t __real_aether_computed_style_handle_clone(aether_computed_style_handle_t,aether_computed_style_handle_t*);
extern "C" aether_status_t __wrap_aether_computed_style_handle_clone(aether_computed_style_handle_t h,aether_computed_style_handle_t* out){if(region)++clones;return __real_aether_computed_style_handle_clone(h,out);}

namespace {
int overflow_calls=0, inject_overflow_at=0;
aether_layout_box_s* first_scroll=nullptr;
float first_previous_x=0,first_previous_y=0;
bool saw_prior_scroll_write=false;
bool observe_overflow=false, observed_real_throw=false;
const aether_layout_box_s* observed_throw_box=nullptr;
}
namespace {
aether_layout_box_s* sample_witness=nullptr;
bool saw_applied_sample=false;
void* numeric_stack[32]{};int numeric_depth=0;
}
extern "C" [[noreturn]] void __real___cxa_throw(void*,void*,void(*)(void*));
extern "C" [[noreturn]] void __wrap___cxa_throw(void* exception,void* type,void(*destroy)(void*)){
    if(observe_overflow){
        if(first_scroll && (first_scroll->scroll.scrollable_overflow_x!=first_previous_x || first_scroll->scroll.scrollable_overflow_y!=first_previous_y))saw_prior_scroll_write=true;
        saw_applied_sample=sample_witness && sample_witness->visual_sample_valid && sample_witness->visual_timestamp==750;
        const bool previous=region;region=false;numeric_depth=backtrace(numeric_stack,32);region=previous;
    }
    __real___cxa_throw(exception,type,destroy);
}
extern "C" void real_extent(const aether_layout_box_s*,bool,bool,float&,float&)
 asm("__real__Z39accumulate_scrollable_descendant_extentPK19aether_layout_box_sbbRfS2_");
extern "C" void wrap_extent(const aether_layout_box_s*,bool,bool,float&,float&)
 asm("__wrap__Z39accumulate_scrollable_descendant_extentPK19aether_layout_box_sbbRfS2_");
extern "C" void wrap_extent(const aether_layout_box_s* box,bool x,bool y,float& right,float& bottom){
    if(observe_overflow){
        if(++overflow_calls==1){first_scroll=const_cast<aether_layout_box_s*>(box);first_previous_x=box->scroll.scrollable_overflow_x;first_previous_y=box->scroll.scrollable_overflow_y;}
        else if(first_scroll && (first_scroll->scroll.scrollable_overflow_x!=first_previous_x || first_scroll->scroll.scrollable_overflow_y!=first_previous_y))saw_prior_scroll_write=true;
        try {real_extent(box,x,y,right,bottom);}
        catch (...) {observed_real_throw=true;observed_throw_box=box;throw;}
        return;
    }
    if(!inject_overflow_at){real_extent(box,x,y,right,bottom);return;}
    if(++overflow_calls==1){first_scroll=const_cast<aether_layout_box_s*>(box);first_previous_x=box->scroll.scrollable_overflow_x;first_previous_y=box->scroll.scrollable_overflow_y;}
    if(overflow_calls==inject_overflow_at){
        saw_prior_scroll_write=first_scroll && (first_scroll->scroll.scrollable_overflow_x!=first_previous_x || first_scroll->scroll.scrollable_overflow_y!=first_previous_y);
        CHECK(!box->children.empty());
        // Corrupt owned test geometry only at this late boundary. The real
        // traversal must discover/reject it; never replace its output/algorithm.
        auto* child=box->children.front();
        struct RestoreGeometry {
            aether_layout_box_s* child; aether_rectf border,margin;
            ~RestoreGeometry(){child->border_box=border;child->margin_box=margin;}
        } restore{child,child->border_box,child->margin_box};
        child->border_box.width=std::numeric_limits<float>::infinity();
        child->margin_box.width=std::numeric_limits<float>::infinity();
        real_extent(box,x,y,right,bottom);
        CHECK(false); // The real finite guard must throw, not hide invalid geometry.
    }
    real_extent(box,x,y,right,bottom);
}
namespace {
struct Saved {
    aether_layout_box_s* box;
    bool valid,has;
    aether_transform_matrix matrix;
    float opacity;
    int64_t timestamp;
    std::weak_ptr<node_identity_s> identity;
    aether_scroll_metrics_t scroll;
};
std::vector<Saved> snapshot(aether_layout_result_t layout){
    std::vector<Saved> s;
    for(auto* b:layout->paint_order)s.push_back({b,b->visual_sample_valid,b->visual_has_transform,b->visual_transform,b->visual_opacity,b->visual_timestamp,b->visual_identity,b->scroll});
    return s;
}
void unchanged(const std::vector<Saved>& s){
    for(const auto& old:s){auto* b=old.box;
        CHECK(b->visual_sample_valid==old.valid && b->visual_has_transform==old.has);
        CHECK(std::memcmp(&b->visual_transform,&old.matrix,sizeof(old.matrix))==0);
        CHECK(std::memcmp(&b->visual_opacity,&old.opacity,sizeof(old.opacity))==0);
        CHECK(b->visual_timestamp==old.timestamp);
        CHECK(!b->visual_identity.owner_before(old.identity) && !old.identity.owner_before(b->visual_identity));
        CHECK(std::memcmp(&b->scroll,&old.scroll,sizeof(old.scroll))==0);
    }
}
struct Fixture {
    aether_doc_t doc{}; aether_node_t node{};
    aether_style_runtime_t styles{}; aether_style_environment_t env{800,600,1,16,0,0,0};
    aether_style_revision_t old_revision{},new_revision{};
    aether_style_map_t old_map{},map{};
    aether_computed_style_handle_t before{},after{};
    aether_contextual_transition_runtime_t runtime{};
    aether_contextual_transition_batch_t batch{},next{};
    aether_layout_engine_t engine{}; aether_layout_result_t layout{};
    aether_style_revision_t revision(){aether_style_transaction_t t{};aether_style_revision_t r{};CHECK(aether_style_runtime_prepare_revision(styles,aether_doc_get_document_element(doc),&env,&t)==AETHER_OK);CHECK(aether_style_runtime_commit_revision(styles,t,&r)==AETHER_OK);aether_style_transaction_destroy(t);return r;}
    explicit Fixture(bool transforms, size_t rich_count=0){
        CHECK(aether_doc_create(&doc)==AETHER_OK);
        std::string html="<html><body><div id='parent'><div id='child'";
        if(rich_count)html+=" class='rich'";
        html+="><div id='leaf'></div></div>";
        for(size_t i=1;i<rich_count;++i)html+="<div class='rich'></div>";
        html+="</div></body></html>";
        CHECK(aether_doc_parse_html(doc,html.c_str())==AETHER_OK);
        node=aether_doc_get_element_by_id(doc,"child");
        aether_style_runtime_options_t opts{};CHECK(aether_style_runtime_create(&opts,&env,nullptr,nullptr,&styles)==AETHER_OK);
        int64_t sheet{};
        CHECK(aether_style_runtime_add_stylesheet(styles,"*{display:block;margin:0;padding:0}#parent{width:100px;height:100px;overflow:auto}#child{width:100px;height:30px;overflow:auto;position:relative;transition:all 1s linear}#leaf{width:180px;height:80px}.rich{width:10px;height:4px;background-image:url('asset://sampler'),linear-gradient(red,blue);text-shadow:1px 1px 2px red;box-shadow:1px 1px 2px blue;backdrop-filter:blur(2px)}",0,"sampler",&sheet,nullptr)==AETHER_OK);
        if(transforms)CHECK(aether_element_set_attribute(node,"style","transform:translateX(0%);opacity:0.2")==AETHER_OK);
        old_revision=revision();CHECK(aether_style_revision_create_map(old_revision,&old_map)==AETHER_OK);before=aether_style_map_get(old_map,node);CHECK(before);
        CHECK(aether_element_set_attribute(node,"style",transforms?"transform:translateX(150%);opacity:0.8":"opacity:0.8")==AETHER_OK);
        new_revision=revision();CHECK(aether_style_revision_create_map(new_revision,&map)==AETHER_OK);after=aether_style_map_get(map,node);CHECK(after);
        CHECK(aether_contextual_transition_create(&runtime)==AETHER_OK);
        aether_contextual_transition_event event{1,0,node,before,after};
        CHECK(aether_contextual_transition_prepare(runtime,&event,1,500,&batch)==AETHER_OK);
        CHECK(aether_contextual_transition_prepare(runtime,&event,1,750,&next)==AETHER_OK);
        CHECK(aether_contextual_transition_batch_has_active(batch)==AETHER_TRUE);
        CHECK(aether_contextual_transition_batch_has_active(next)==AETHER_TRUE);
        CHECK(bool(aether_contextual_transition_active_transform(batch,node))==transforms);
        CHECK(bool(aether_contextual_transition_active_transform(next,node))==transforms);
        aether_contextual_transition_sample active_sample{};
        CHECK(aether_contextual_transition_sample_box(next,after,{0,0,100,30},&active_sample)==AETHER_OK);
        CHECK(active_sample.active==AETHER_TRUE && active_sample.presentation_timestamp_ms==750);
        if(transforms)CHECK(active_sample.matrix.dx==112.5f);
        CHECK(aether_layout_create(nullptr,nullptr,&engine)==AETHER_OK);
        CHECK(aether_contextual_transition_prepare_layout(engine,batch)==AETHER_OK);
        CHECK(aether_layout_layout(engine,doc,map,800,600,nullptr,nullptr,&layout)==AETHER_OK);
        CHECK(aether_contextual_transition_finalize_layout(layout,batch)==AETHER_OK);
    }
    ~Fixture(){aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_contextual_transition_batch_destroy(next);aether_contextual_transition_batch_destroy(batch);aether_contextual_transition_destroy(runtime);aether_computed_style_handle_destroy(after);aether_computed_style_handle_destroy(before);aether_style_map_destroy(map);aether_style_map_destroy(old_map);aether_style_revision_destroy(new_revision);aether_style_revision_destroy(old_revision);aether_style_runtime_destroy(styles);aether_doc_destroy(doc);}
};
void provenance_visual(aether_layout_box_s* box,const aether_contextual_transition_sample& sample){
    CHECK(box->visual_sample_valid);
    CHECK(box->visual_has_transform==bool(sample.has_transform));
    CHECK(box->visual_transform.m11==sample.matrix.m11);
    CHECK(box->visual_transform.m12==sample.matrix.m12);
    CHECK(box->visual_transform.m21==sample.matrix.m21);
    CHECK(box->visual_transform.m22==sample.matrix.m22);
    CHECK(box->visual_transform.dx==sample.matrix.dx);
    CHECK(box->visual_transform.dy==sample.matrix.dy);
    CHECK(box->visual_opacity==sample.opacity);
    CHECK(box->visual_timestamp==sample.presentation_timestamp_ms);
    const std::weak_ptr<node_identity_s> identity=box->element->identity;
    CHECK(!box->visual_identity.owner_before(identity)&&!identity.owner_before(box->visual_identity));
}
void provenance_retained_clone(){
    Fixture f(true);
    auto* source=aether_layout_result_find_primary_box(f.layout,f.node);CHECK(source);
    const auto source_box=source->border_box;
    const auto source_state=snapshot(f.layout);
    aether_layout_result_t clone=nullptr;
    CHECK(aether_layout_clone_snapshot(f.layout,&clone)==AETHER_OK);
    CHECK(clone->assets==f.layout->assets);
    auto* target=aether_layout_result_find_primary_box(clone,f.node);
    CHECK(target&&target!=source);
    target->border_box.width=source_box.width+137;
    CHECK(source->border_box.width==source_box.width);
    aether_contextual_transition_sample expected{};
    CHECK(aether_contextual_transition_sample_box(f.next,f.after,target->border_box,&expected)==AETHER_OK);
    const auto retained_base=clone->assets->base;
    // Public flat is independently owned; adversarial edits cannot alter pins.
    CHECK(f.after->style.transforms.count>0);
    f.after->style.transforms.items[0].x.value=-999;
    f.after->style.opacity=.13f;
    // Mutation and destruction concern external wrappers, not the captured map.
    f.map->current=f.old_map->current;
    CHECK(f.map->current!=retained_base);
    aether_style_map_destroy(f.map);f.map=nullptr;
    aether_style_map_destroy(f.old_map);f.old_map=nullptr;
    aether_computed_style_handle_destroy(f.after);f.after=nullptr;
    aether_computed_style_handle_destroy(f.before);f.before=nullptr;
    aether_style_revision_destroy(f.new_revision);f.new_revision=nullptr;
    aether_style_revision_destroy(f.old_revision);f.old_revision=nullptr;
    aether_style_runtime_destroy(f.styles);f.styles=nullptr;
    CHECK(clone->assets->base==retained_base);
    CHECK(aether_contextual_transition_finalize_layout(clone,f.next)==AETHER_OK);
    provenance_visual(target,expected);
    unchanged(source_state);
    CHECK(source->border_box.width==source_box.width);
    // Clone remains valid after source geometry is released.
    aether_layout_result_destroy(f.layout);f.layout=nullptr;
    CHECK(aether_contextual_transition_finalize_layout(clone,f.next)==AETHER_OK);
    provenance_visual(target,expected);
    aether_layout_result_destroy(clone);
}
void provenance_map_modes(){
    using namespace aether::style;
    Fixture f(true);
    const auto original=f.layout->assets->base;
    CHECK(original&&original->contextual()&&original->revision());
    const auto revision=original->revision();
    // These are authentic runtime records/revision owners, not legacy records
    // relabeled contextual. make_map_version validates exact owner/provenance.
    for(auto mode:{MapMode::contextual,MapMode::legacy_complete,MapMode::legacy_partial}){
        MapVersionInput input;
        input.mode=mode;input.backing=revision->backing();
        input.document_lifetime=revision->document_lifetime();
        input.document_identity=revision->document_identity();
        input.structure_epoch=revision->structure_epoch();
        input.input_epoch=revision->input_epoch();input.style_tree_epoch=revision->style_tree_epoch();
        if(mode!=MapMode::legacy_partial){input.revision=revision;input.generation=revision->generation();}
        aether_style_map_s selected;
        CHECK(make_map_version(input,selected.current)==AETHER_OK);
        CHECK(selected.current->mode()==mode);
        aether_layout_result_t frame=nullptr;
        CHECK(aether_layout_layout(f.engine,f.doc,&selected,800,600,nullptr,nullptr,&frame)==AETHER_OK);
        auto* box=aether_layout_result_find_primary_box(frame,f.node);CHECK(box);
        aether_contextual_transition_sample expected{};
        aether_computed_style_handle_t reference=nullptr;
        CHECK(export_style_handle(*selected.current,selected.current->backing()->find_owner(f.node),&reference)==AETHER_OK);
        CHECK(aether_contextual_transition_sample_box(f.next,reference,box->border_box,&expected)==AETHER_OK);
        aether_computed_style_handle_destroy(reference);
        selected.current.reset();
        CHECK(aether_contextual_transition_finalize_layout(frame,f.next)==AETHER_OK);
        provenance_visual(box,expected);
        aether_layout_result_destroy(frame);
    }
    // Partial map can retain valid records while its global identity is absent.
    // It must fail sampling exactly as its exported public reference does.
    MapVersionInput input;input.backing=original->backing();
    aether_style_map_s unbound;CHECK(make_map_version(input,unbound.current)==AETHER_OK);
    aether_layout_result_t frame=nullptr;
    CHECK(aether_layout_layout(f.engine,f.doc,&unbound,800,600,nullptr,nullptr,&frame)==AETHER_OK);
    aether_computed_style_handle_t reference=nullptr;
    CHECK(export_style_handle(*unbound.current,unbound.current->backing()->find_owner(f.node),&reference)==AETHER_OK);
    auto* unbound_box=aether_layout_result_find_primary_box(frame,f.node);CHECK(unbound_box);
    aether_contextual_transition_sample invalid_reference{};
    CHECK(aether_contextual_transition_sample_box(f.next,reference,unbound_box->border_box,&invalid_reference)==AETHER_ERROR_INVALID_STATE);
    aether_computed_style_handle_destroy(reference);
    auto old=snapshot(frame);
    CHECK(aether_contextual_transition_finalize_layout(frame,f.next)==AETHER_ERROR_INVALID_STATE);
    unchanged(old);
    aether_layout_result_destroy(frame);
}
void provenance_synthetic_text(){
    aether_doc_t doc=nullptr;aether_style_runtime_t runtime=nullptr;
    aether_style_map_t map=nullptr;aether_style_revision_t revision=nullptr;
    aether_layout_engine_t engine=nullptr;aether_layout_result_t frame=nullptr;
    CHECK(aether_doc_create(&doc)==AETHER_OK);
    CHECK(aether_doc_parse_html(doc,"<html><body><div id='flex'>anonymous text</div><p id='plain'>plain text</p></body></html>")==AETHER_OK);
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{800,600,1,16,0,0,0};
    CHECK(aether_style_runtime_create(&options,&environment,nullptr,nullptr,&runtime)==AETHER_OK);
    int64_t sheet=0;
    CHECK(aether_style_runtime_add_stylesheet(runtime,
        "*{margin:0;padding:0}#flex{display:flex;transform:translateX(23px);opacity:.4}#plain{display:block;transform:translateX(31px);opacity:.6}",
        0,"sampler-provenance",&sheet,nullptr)==AETHER_OK);
    aether_style_transaction_t transaction=nullptr;
    CHECK(aether_style_runtime_prepare_revision(runtime,aether_doc_get_document_element(doc),&environment,&transaction)==AETHER_OK);
    CHECK(aether_style_runtime_commit_revision(runtime,transaction,&revision)==AETHER_OK);
    aether_style_transaction_destroy(transaction);
    CHECK(aether_style_revision_create_map(revision,&map)==AETHER_OK);
    CHECK(aether_layout_create(+[](const char*,const aether_computed_style_t*,float,void*){
        return aether_text_metrics_t{70,20,15};},nullptr,&engine)==AETHER_OK);
    CHECK(aether_layout_layout(engine,doc,map,800,600,nullptr,nullptr,&frame)==AETHER_OK);
    auto* flex=aether_doc_get_element_by_id(doc,"flex");
    CHECK(frame->anonymous_flex_items.at(flex).size()==1);
    auto* anonymous=frame->anonymous_flex_items.at(flex)[0];
    CHECK(anonymous->anonymous_flex_owner==flex);
    CHECK(frame->assets->synthetic->find(anonymous));
    CHECK(!frame->assets->base->backing()->find(anonymous));
    aether_layout_result_t clone=nullptr;
    CHECK(aether_layout_clone_snapshot(frame,&clone)==AETHER_OK);
    CHECK(clone->assets==frame->assets);
    aether_style_map_destroy(map);map=nullptr;
    aether_style_revision_destroy(revision);revision=nullptr;
    aether_style_runtime_destroy(runtime);runtime=nullptr;
    aether_layout_result_destroy(frame);frame=nullptr;
    int texts=0,anonymous_boxes=0;
    for(auto* box:clone->paint_order){
        if(!box->element)continue;
        if(box->kind==LAYOUT_BOX_TEXT||box->element->anonymous_flex_owner){
            box->visual_sample_valid=false;box->visual_has_transform=true;
            box->visual_transform={9,8,7,6,5,4};box->visual_opacity=.17f;box->visual_timestamp=-123;
            if(box->kind==LAYOUT_BOX_TEXT)++texts;
            if(box->element==anonymous)++anonymous_boxes;
        }
    }
    CHECK(texts>0&&anonymous_boxes>0);
    CHECK(aether_contextual_transition_finalize_layout(clone,nullptr)==AETHER_OK);
    for(auto* box:clone->paint_order){
        if(!box->element)continue;
        if(box->kind==LAYOUT_BOX_TEXT||box->element->anonymous_flex_owner){
            const aether_contextual_transition_sample identity{{1,0,0,1,0,0},1,AETHER_FALSE,AETHER_FALSE,0};
            provenance_visual(box,identity);
        }
    }
    auto* flex_box=aether_layout_result_find_primary_box(clone,flex);CHECK(flex_box);
    CHECK(flex_box->visual_has_transform&&flex_box->visual_transform.dx==23);
    CHECK(std::abs(flex_box->visual_opacity-.4f)<.00001f);
    aether_layout_result_destroy(clone);aether_layout_destroy(engine);aether_doc_destroy(doc);
}
void provenance_no_map(){
    for(bool explicit_empty:{false,true}){
        aether_doc_t doc=nullptr;aether_layout_engine_t engine=nullptr;aether_layout_result_t frame=nullptr;
        CHECK(aether_doc_create(&doc)==AETHER_OK);
        CHECK(aether_doc_parse_html(doc,"<html><body><div></div></body></html>")==AETHER_OK);
        CHECK(aether_layout_create(nullptr,nullptr,&engine)==AETHER_OK);
        aether_style_map_s empty;
        CHECK(aether_layout_layout(engine,doc,explicit_empty?&empty:nullptr,800,600,nullptr,nullptr,&frame)==AETHER_OK);
        CHECK(!frame->assets->base);
        CHECK(frame->assets->document_lifetime==doc->identity);
        // Missing records skip visual publication; overflow is a separate pass.
        for(auto* box:frame->paint_order){box->visual_sample_valid=false;box->visual_has_transform=true;
            box->visual_transform={9,8,7,6,5,4};box->visual_opacity=.17f;box->visual_timestamp=-123;}
        const auto before=snapshot(frame);
        CHECK(aether_contextual_transition_finalize_layout(frame,nullptr)==AETHER_OK);
        for(const auto& old:before){auto* box=old.box;
            CHECK(box->visual_sample_valid==old.valid&&box->visual_has_transform==old.has);
            CHECK(std::memcmp(&box->visual_transform,&old.matrix,sizeof(old.matrix))==0);
            CHECK(box->visual_opacity==old.opacity&&box->visual_timestamp==old.timestamp);
            CHECK(!box->visual_identity.owner_before(old.identity)&&!old.identity.owner_before(box->visual_identity));}
        aether_doc_destroy(doc);doc=nullptr;
        const auto stale=snapshot(frame);
        CHECK(aether_contextual_transition_finalize_layout(frame,nullptr)==AETHER_ERROR_INVALID_STATE);
        unchanged(stale);
        aether_layout_result_destroy(frame);aether_layout_destroy(engine);
    }
}

void allocator_controls(){
    // Opaque calls retain real external relocations and prevent builtin folding
    // from hiding either the calls or the observed counter side effects.
    void* (*volatile cpp_allocate)(size_t)=static_cast<void*(*)(size_t)>(&::operator new);
    void* (*volatile c_allocate)(size_t)=&std::malloc;
    void* (*volatile c_zero)(size_t,size_t)=&std::calloc;
    void* (*volatile c_resize)(void*,size_t)=&std::realloc;
    reset();region=true;void* cpp=cpp_allocate(13);region=false;
    CHECK(cpp && calls[0]==1 && calls[1]==0);::operator delete(cpp);
    reset();region=true;void* p=c_allocate(13);void* q=c_zero(2,9);
    CHECK(p && q);p=c_resize(p,19);region=false;
    CHECK(p && calls[0]==0 && calls[1]==1 && calls[2]==1 && calls[3]==1);
    std::free(p);std::free(q);
    std::puts("allocator controls: new=1 nested_malloc=0; direct malloc=1 calloc=1 realloc=1");
}
void handle_control(Fixture& f){
    reset();region=true;auto h=aether_style_map_get(f.map,f.node);region=false;CHECK(h);CHECK(gets==1);
    std::printf("map_get positive: gets=%zu nested_exports=%zu (same-TU calls need not interpose)\n",gets,exports);
    aether_computed_style_handle_t cloned{};reset();region=true;auto status=aether_computed_style_handle_clone(h,&cloned);region=false;CHECK(status==AETHER_OK && clones==1);
    reset();region=true;aether_computed_style_handle_destroy(h);aether_computed_style_handle_destroy(cloned);region=false;CHECK(destroys==2);
    auto version=f.layout->assets->base;auto record=version->backing()->find_owner(f.node);
    reset();region=true;status=aether::style::export_style_handle(*version,record,&h);region=false;CHECK(status==AETHER_OK && exports==1);aether_computed_style_handle_destroy(h);
    std::puts("positive controls: map_get=1 export=1 clone=1 destroy=2 direct_export=1");
}
void check_handles(bool baseline,bool success){
    CHECK(gets==0 && clones==0);
    if(!baseline)CHECK(exports==0 && destroys==0);
    else if(success)CHECK(exports>0 && destroys==exports);
}
void sweep(Fixture& f,bool baseline,bool transforms,aether_contextual_transition_batch_t target){
    CHECK(aether_contextual_transition_finalize_layout(f.layout,f.batch)==AETHER_OK);
    reset();region=true;auto status=aether_contextual_transition_finalize_layout(f.layout,target);region=false;
    CHECK(status==AETHER_OK);check_handles(baseline,true);
    size_t counts[4];std::memcpy(counts,calls,sizeof(counts));
    std::printf("clean transforms=%d batch=%d new=%zu malloc=%zu calloc=%zu realloc=%zu exports=%zu destroy=%zu\n",transforms,target!=nullptr,counts[0],counts[1],counts[2],counts[3],exports,destroys);
    auto expected=snapshot(f.layout);
    for(int fam=0;fam<4;++fam)for(size_t i=0;i<counts[fam];++i){
        CHECK(aether_contextual_transition_finalize_layout(f.layout,f.batch)==AETHER_OK);
        auto saved=snapshot(f.layout);reset(fam,int(i));region=true;
        status=aether_contextual_transition_finalize_layout(f.layout,target);region=false;
        CHECK(status==AETHER_ERROR_OUT_OF_MEMORY);check_handles(baseline,false);unchanged(saved);CHECK(failure_depth>0);
        std::printf("failure transforms=%d batch=%d family=%d ordinal=%zu stack",transforms,target!=nullptr,fam,i);
        for(int j=0;j<failure_depth;++j)std::printf(" %p",failure_stack[j]);std::puts("");
        reset();CHECK(aether_contextual_transition_finalize_layout(f.layout,target)==AETHER_OK);unchanged(expected);
    }
    CHECK(counts[0]>=2); // Both staging vectors must exist, also for empty transforms.
}
void scaled_handles(bool baseline,size_t count){
    Fixture f(true,count);size_t rich=0;
    for(const auto& [node,record]:f.layout->assets->base->backing()->entries()){
        const auto view=record->style_view();
        if(view.background_layers().size()==2){
            CHECK(!view.box_shadows().empty() && !view.text_shadows().empty() && !view.backdrop_filters().empty());
            CHECK(view.background_layers()[0].view().image_source.present());
            CHECK(view.background_layers()[1].view().gradient.stops.size()>=2);++rich;
        }
    }
    CHECK(rich==count);
    for(auto batch:{static_cast<aether_contextual_transition_batch_t>(nullptr),f.next}){
        reset();region=true;const auto status=aether_contextual_transition_finalize_layout(f.layout,batch);region=false;
        CHECK(status==AETHER_OK);check_handles(baseline,true);
        std::printf("scaled rich=%zu total_boxes=%zu batch=%d export=%zu get=%zu clone=%zu destroy=%zu new=%zu malloc=%zu\n",count,f.layout->paint_order.size(),batch!=nullptr,exports,gets,clones,destroys,calls[0],calls[1]);
    }
}
void finite_overflow_rollback(Fixture& f,bool baseline){
    auto* leaf=aether_layout_result_find_primary_box(f.layout,aether_doc_get_element_by_id(f.doc,"leaf"));
    auto* child=aether_layout_result_find_primary_box(f.layout,f.node);CHECK(leaf && child);
    const auto previous=leaf->margin_box;
    const float large=std::numeric_limits<float>::max()*.75f;
    leaf->margin_box.x=large;leaf->margin_box.width=large;
    CHECK(std::isfinite(leaf->margin_box.x) && std::isfinite(leaf->margin_box.width));
    CHECK(double(leaf->margin_box.x)+leaf->margin_box.width>std::numeric_limits<float>::max());
    for(auto* box:f.layout->paint_order){box->scroll.scrollable_overflow_x=-37;box->scroll.scrollable_overflow_y=-41;}
    auto saved=snapshot(f.layout);
    overflow_calls=0;first_scroll=nullptr;saw_prior_scroll_write=false;observed_real_throw=false;observed_throw_box=nullptr;
    reset();observe_overflow=true;region=true;
    const auto status=aether_contextual_transition_finalize_layout(f.layout,f.next);
    region=false;observe_overflow=false;
    CHECK(status==AETHER_ERROR_INVALID_STATE && observed_real_throw && observed_throw_box==child);
    CHECK(overflow_calls>=2 && saw_prior_scroll_write);check_handles(baseline,false);unchanged(saved);
    // Observation did not mutate geometry; restore the explicit fixture input.
    CHECK(leaf->margin_box.x==large && leaf->margin_box.width==large);
    leaf->margin_box=previous;CHECK(aether_contextual_transition_finalize_layout(f.layout,f.next)==AETHER_OK);
    std::printf("finite overflow: leaf x=%g width=%g; real extent throw after %d entries and prior scroll write; complete rollback/retry PASS\n",double(large),double(large),overflow_calls);
}
void numeric_matrix(Fixture& f,bool baseline){
    auto* root=f.layout->root;
    auto* parent=aether_layout_result_find_primary_box(f.layout,aether_doc_get_element_by_id(f.doc,"parent"));
    auto* child=aether_layout_result_find_primary_box(f.layout,f.node);
    const float large=std::numeric_limits<float>::max()*.75f,inf=std::numeric_limits<float>::infinity();
    const char* names[]={"content.x","content.y","content.width","content.height","initial_right finite sum","initial_bottom finite sum","subtract_x finite overflow","root viewport height nonfinite","root subtract_y finite overflow","final unmodified overflow_x","final unmodified overflow_y"};
    for(int which=0;which<11;++which){
        CHECK(aether_contextual_transition_finalize_layout(f.layout,f.batch)==AETHER_OK);
        const auto parent_content=parent->content_box,root_content=root->content_box,child_margin=child->margin_box;
        const auto viewport=f.layout->viewport;
        const auto old_x=parent->style.overflow_x,old_y=parent->style.overflow_y;
        switch(which){
        case 0:parent->content_box.x=inf;break;
        case 1:parent->content_box.y=inf;break;
        case 2:parent->content_box.width=inf;break;
        case 3:parent->content_box.height=inf;break;
        case 4:parent->content_box.x=large;parent->content_box.width=large;break;
        case 5:parent->content_box.y=large;parent->content_box.height=large;break;
        case 6:parent->content_box.x=-large;parent->content_box.width=0;child->margin_box.x=large;child->margin_box.width=0;break;
        case 7:f.layout->viewport.height=inf;break;
        case 8:root->content_box.y=-large;f.layout->viewport.height=-large;break;
        case 9:parent->style.overflow_x=AETHER_OVERFLOW_CLIP;parent->style.overflow_y=AETHER_OVERFLOW_AUTO;break;
        case 10:parent->style.overflow_y=AETHER_OVERFLOW_CLIP;parent->style.overflow_x=AETHER_OVERFLOW_AUTO;break;
        }
        for(auto* box:f.layout->paint_order){box->scroll.scrollable_overflow_x=-37;box->scroll.scrollable_overflow_y=-41;}
        if(which==9)parent->scroll.scrollable_overflow_x=std::numeric_limits<float>::quiet_NaN();
        if(which==10)parent->scroll.scrollable_overflow_y=std::numeric_limits<float>::quiet_NaN();
        const auto saved=snapshot(f.layout);
        overflow_calls=0;first_scroll=nullptr;saw_prior_scroll_write=false;saw_applied_sample=false;numeric_depth=0;sample_witness=child;
        reset();observe_overflow=true;region=true;
        const auto status=aether_contextual_transition_finalize_layout(f.layout,f.next);
        region=false;observe_overflow=false;
        CHECK(status==AETHER_ERROR_INVALID_STATE && saw_applied_sample && numeric_depth>0);
        // Root y checks also observe its already-written x overflow.
        CHECK(saw_prior_scroll_write);check_handles(baseline,false);unchanged(saved);
        std::printf("numeric case=%s extents=%d sample_applied=1 prior_scroll_write=1 stack",names[which],overflow_calls);
        for(int i=0;i<numeric_depth;++i)std::printf(" %p",numeric_stack[i]);std::puts("");
        parent->content_box=parent_content;root->content_box=root_content;child->margin_box=child_margin;f.layout->viewport=viewport;parent->style.overflow_x=old_x;parent->style.overflow_y=old_y;
        CHECK(aether_contextual_transition_finalize_layout(f.layout,f.next)==AETHER_OK);
    }
    sample_witness=nullptr;
}
void late_rollback(Fixture& f,bool baseline){
    // Sentinel scroll values make an earlier completed write observable to wrapper.
    for(auto* box:f.layout->paint_order){box->scroll.scrollable_overflow_x=-37;box->scroll.scrollable_overflow_y=-41;}
    auto saved=snapshot(f.layout);
    overflow_calls=0;inject_overflow_at=2;first_scroll=nullptr;saw_prior_scroll_write=false;
    reset();region=true;auto status=aether_contextual_transition_finalize_layout(f.layout,f.next);region=false;inject_overflow_at=0;
    CHECK(status==AETHER_ERROR_INVALID_STATE && overflow_calls==2 && saw_prior_scroll_write);
    check_handles(baseline,false);unchanged(saved);
    CHECK(aether_contextual_transition_finalize_layout(f.layout,f.next)==AETHER_OK);
    // Real invalid geometry is separately rejected after sampling, without output injection.
    auto* root=f.layout->root;const auto previous=root->content_box;
    root->content_box.width=std::numeric_limits<float>::infinity();saved=snapshot(f.layout);
    CHECK(aether_contextual_transition_finalize_layout(f.layout,f.batch)==AETHER_ERROR_INVALID_STATE);unchanged(saved);
    root->content_box=previous;CHECK(aether_contextual_transition_finalize_layout(f.layout,f.batch)==AETHER_OK);
    std::puts("nonfinite geometry injection: prior scroll write observed; complete rollback/retry PASS");
}
}
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    const bool baseline=argc==2 && std::strcmp(argv[1],"--baseline")==0;CHECK(argc==1 || baseline);
    void* warm[32];backtrace(warm,32);allocator_controls();
    for(bool transforms:{false,true}){Fixture fixture(transforms);handle_control(fixture);sweep(fixture,baseline,transforms,nullptr);sweep(fixture,baseline,transforms,fixture.next);finite_overflow_rollback(fixture,baseline);if(!baseline)numeric_matrix(fixture,false);late_rollback(fixture,baseline);}
    if(!baseline){provenance_retained_clone();provenance_map_modes();provenance_synthetic_text();provenance_no_map();std::puts("provenance retained/clone/map modes/synthetic/no-map PASS");}
    if(!baseline)for(size_t count:{size_t(1),size_t(100),size_t(1000)})scaled_handles(false,count);
    std::puts(baseline?"sampler genuine baseline PASS":"sampler no public handles atomic PASS");
}
