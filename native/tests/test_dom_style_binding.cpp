#include <aether/aether_dom_style_binding.h>
#include <aether/aether_contextual_style.h>
#include <aether/aether_dom_sync.h>
#include "../src/dom/dom_types.h"
#include "../src/style_runtime/aether_dom_style_binding.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
static long fail_after=-1;
static bool fault_delivered=false;
void* operator new(size_t n) { if(fail_after>=0 && fail_after--==0){fault_delivered=true;throw std::bad_alloc();}if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc(); }
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}void operator delete[](void* p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&)noexcept{try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&)noexcept{std::free(p);}void operator delete[](void* p,const std::nothrow_t&)noexcept{std::free(p);}
#include "fault_inject_aligned.h"
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static void ok(aether_status_t s){CHECK(s==AETHER_OK);}
struct Slot {int64_t handle;int32_t present;uint64_t epoch;};
static Slot slot(aether_dom_style_binding_t b){Slot s{};ok(aether_dom_style_binding_read_slot_v1(b,&s.handle,&s.present,&s.epoch));return s;}
struct Fixture {
    aether_doc_t doc=nullptr;aether_style_runtime_t runtime=nullptr;aether_dom_style_binding_t binding=nullptr;
    Fixture(const char* markup="<body><style id='s'>#x{opacity:.2}</style><div id='x'></div></body>"){
        ok(aether_doc_create(&doc));ok(aether_doc_parse_html(doc,markup));ok(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&runtime));
    }
    void bind(){ok(aether_dom_style_binding_create_v1(doc,runtime,&binding));}
    ~Fixture(){aether_dom_style_binding_destroy_v1(binding);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
    DomStyleSlotStageRef prepare(const std::string& source){DomStyleSlotStageRef s;ok(aether_dom_style_prepare_slot(doc->owner_control->binding.lock(),source,s));return s;}
    void publish(DomStyleSlotStageRef& s){fail_after=0;CHECK(aether_dom_style_validate_slot(s));aether_dom_style_commit_slot(s);fail_after=-1;}
    void resolve(){aether_style_transaction_t t=nullptr;ok(aether_style_runtime_prepare_revision(runtime,doc,nullptr,&t));aether_style_revision_t r=nullptr;ok(aether_style_runtime_commit_revision(runtime,t,&r));aether_style_revision_destroy(r);aether_style_transaction_destroy(t);}
    float opacity(){aether_style_transaction_t t=nullptr;ok(aether_style_runtime_prepare_revision(runtime,doc,nullptr,&t));aether_style_revision_t r=nullptr;ok(aether_style_runtime_commit_revision(runtime,t,&r));float value=-1;for(int i=0;i<aether_style_revision_style_count(r);++i){aether_node_t n=nullptr;aether_computed_style_handle_t h=nullptr;ok(aether_style_revision_get_style(r,i,&n,&h,nullptr));if(n==aether_doc_get_element_by_id(doc,"x")){aether_computed_style_t v{};ok(aether_computed_style_copy(h,&v));value=v.opacity;aether_computed_style_free(&v);}aether_computed_style_handle_destroy(h);}aether_style_revision_destroy(r);aether_style_transaction_destroy(t);return value;}
};
static void removal_and_order(){
    Fixture f;int64_t before=0,after=0;ok(aether_style_runtime_add_stylesheet(f.runtime,"#x{opacity:.1}",2,"before",&before,nullptr));f.bind();auto first=slot(f.binding);CHECK(std::fabs(f.opacity()-.2f)<.001f);
    ok(aether_style_runtime_add_stylesheet(f.runtime,"#x{opacity:.8}",2,"after",&after,nullptr));CHECK(std::fabs(f.opacity()-.8f)<.001f);
    auto s=f.prepare("#x{opacity:.4} /* harmless relative to foreign sheet */\n");f.publish(s);CHECK(slot(f.binding).handle==first.handle);CHECK(std::fabs(f.opacity()-.8f)<.001f);
    CHECK(aether_style_runtime_remove_stylesheet(f.runtime,first.handle));auto removed=slot(f.binding);CHECK(!removed.present);
    s=f.prepare("#x{opacity:.4} /* harmless relative to foreign sheet */\n");f.publish(s);CHECK(slot(f.binding).epoch==removed.epoch&&!slot(f.binding).present);CHECK(std::fabs(f.opacity()-.8f)<.001f);
    s=f.prepare("#x{opacity:.6}\n");f.publish(s);CHECK(slot(f.binding).handle>after&&slot(f.binding).present);CHECK(std::fabs(f.opacity()-.6f)<.001f);
    CHECK(aether_style_runtime_remove_stylesheet(f.runtime,slot(f.binding).handle));CHECK(std::fabs(f.opacity()-.8f)<.001f);CHECK(aether_style_runtime_remove_stylesheet(f.runtime,after));CHECK(std::fabs(f.opacity()-.1f)<.001f);
}
static void identity_and_disposal(){
    Fixture f;f.bind();aether_dom_style_binding_t other=nullptr;CHECK(aether_dom_style_binding_create_v1(f.doc,f.runtime,&other)==AETHER_ERROR_INVALID_STATE&&!other);
    aether_style_runtime_t unrelated=nullptr;ok(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&unrelated));aether_style_update_t* updates=nullptr;int n=aether_style_runtime_resolve_subtree(unrelated,f.doc,&updates);CHECK(n>0);aether_style_updates_free(updates,n);aether_style_runtime_destroy(unrelated);
    aether_doc_t second=nullptr;ok(aether_doc_create(&second));ok(aether_doc_parse_html(second,"<body><style>div{width:2px}</style><div></div></body>"));ok(aether_dom_style_binding_create_v1(second,f.runtime,&other));CHECK(slot(other).handle!=slot(f.binding).handle);aether_dom_style_binding_destroy_v1(other);aether_doc_destroy(second);
    auto s=f.prepare("#x{opacity:.5}");bool valid=true;std::thread worker([&]{valid=aether_dom_style_validate_slot(s);});worker.join();CHECK(!valid);
    aether_dom_style_binding_destroy_v1(f.binding);f.binding=nullptr;CHECK(!aether_dom_style_validate_slot(s));f.bind();CHECK(!aether_dom_style_validate_slot(s));s=f.prepare("#x{opacity:.7}");aether_style_runtime_destroy(f.runtime);f.runtime=nullptr;CHECK(!aether_dom_style_validate_slot(s));CHECK(f.doc->owner_control->binding.expired());
}
struct Diagnostics {Fixture* fixture;int count=0,mode=0;static void receive(const char*,const char*,const char*,const char*,void* data){auto& d=*static_cast<Diagnostics*>(data);++d.count;if(d.count!=1)return;auto& f=*d.fixture;if(d.mode==1){aether_element_set_state(aether_doc_get_element_by_id(f.doc,"x"),1);f.resolve();}if(d.mode==2){auto s=f.prepare("#x{opacity:.9}");f.publish(s);}if(d.mode==3)aether_style_runtime_remove_stylesheet(f.runtime,slot(f.binding).handle);if(d.mode==4)aether_style_runtime_set_diagnostics_callback(f.runtime,nullptr,nullptr);if(d.mode==5){aether_style_runtime_destroy(f.runtime);f.runtime=nullptr;}}};
static const char* invalid_css="#x{width:calc(1px + 1deg);height:calc(2px + 2deg)}";
static void diagnostic_replay(){for(int mode=0;mode<=5;++mode){Fixture f;Diagnostics d{&f,0,mode};aether_style_runtime_set_diagnostics_callback(f.runtime,Diagnostics::receive,&d);f.bind();auto s=f.prepare(invalid_css);f.publish(s);CHECK(d.count==0);f.resolve();CHECK(d.count==(mode<=1?2:1));if(f.runtime){f.resolve();CHECK(d.count==(mode<=1?2:1));}}}
static void fault_atomicity(){Fixture f;Diagnostics d{&f};aether_style_runtime_set_diagnostics_callback(f.runtime,Diagnostics::receive,&d);f.bind();auto baseline=slot(f.binding);const std::string source="#x{opacity:.7;width:calc(1px + 1deg)}";bool succeeded=false;for(long i=0;i<1500;++i){DomStyleSlotStageRef s;fault_delivered=false;fail_after=i;auto result=aether_dom_style_prepare_slot(f.doc->owner_control->binding.lock(),source,s);fail_after=-1;if(result==AETHER_OK){f.publish(s);succeeded=true;break;}CHECK(result==AETHER_ERROR_OUT_OF_MEMORY&&fault_delivered&&!s);auto current=slot(f.binding);CHECK(current.epoch==baseline.epoch&&current.handle==baseline.handle&&current.present);CHECK(std::fabs(f.opacity()-.2f)<.001f);}CHECK(succeeded);}
#ifdef AETHER_DOM_STYLE_TESTING
static void exhaustion(){Fixture f;f.bind();auto old=slot(f.binding);aether_dom_style_test_set_limits(f.runtime,UINT64_MAX,100,100);DomStyleSlotStageRef s;CHECK(aether_dom_style_prepare_slot(f.doc->owner_control->binding.lock(),"#x{opacity:.6}",s)==AETHER_ERROR_INVALID_STATE);CHECK(slot(f.binding).epoch==UINT64_MAX&&slot(f.binding).handle==old.handle);aether_dom_style_test_set_limits(f.runtime,old.epoch,INT64_MAX,INT32_MAX);CHECK(aether_dom_style_prepare_slot(f.doc->owner_control->binding.lock(),"#x{opacity:.6}",s)==AETHER_ERROR_INVALID_STATE);CHECK(slot(f.binding).handle==old.handle);}
#endif
static void all_cascade_positions(){
    Fixture f("<body><div id='x'></div></body>");f.bind();
    auto source = std::string("#x,.missing{--n:3px;--m:4px;opacity:.3;width:var(--n);margin:var(--m)}#x{opacity:.4}#x::before{content:'slot'}");
    auto stage=f.prepare(source);f.publish(stage);int64_t foreign=0;
    ok(aether_style_runtime_add_stylesheet(f.runtime,"#x{--n:9px;--m:8px;opacity:.8}#x::before{content:'foreign'}",2,"foreign",&foreign,nullptr));
    for(int pass=0;pass<2;++pass){
        if(pass){stage=f.prepare(source+" /* source changed, same sheet position */");f.publish(stage);}
        aether_style_transaction_t t=nullptr;ok(aether_style_runtime_prepare_revision(f.runtime,f.doc,nullptr,&t));aether_style_revision_t r=nullptr;ok(aether_style_runtime_commit_revision(f.runtime,t,&r));
        bool found=false,pseudo=false;auto* target=aether_doc_get_element_by_id(f.doc,"x");
        for(int i=0;i<aether_style_revision_style_count(r);++i){aether_node_t n=nullptr;aether_computed_style_handle_t h=nullptr;ok(aether_style_revision_get_style(r,i,&n,&h,nullptr));
            if(n==target){aether_computed_style_t v{};ok(aether_computed_style_copy(h,&v));CHECK(std::fabs(v.opacity-.8f)<.001f&&std::fabs(v.width.value-9)<.001f&&std::fabs(v.margin.left.value-8)<.001f);aether_computed_style_free(&v);found=true;}
            if(n->style_origin==target&&n->pseudo_element==aether_pseudo_element_t::before){CHECK(!n->children.empty()&&n->children.front()->text=="foreign");pseudo=true;}aether_computed_style_handle_destroy(h);
        }CHECK(found&&pseudo);aether_style_revision_destroy(r);aether_style_transaction_destroy(t);
    }
    CHECK(aether_style_runtime_remove_stylesheet(f.runtime,foreign));CHECK(std::fabs(f.opacity()-.4f)<.001f);
}
static void batch_and_rebind(){
    Fixture f;f.bind();aether_doc_t second=nullptr;ok(aether_doc_create(&second));ok(aether_doc_parse_html(second,"<body><style>#x{opacity:.3}</style><div id='x'></div></body>"));aether_dom_style_binding_t b=nullptr;ok(aether_dom_style_binding_create_v1(second,f.runtime,&b));
    auto a=f.prepare("#x{opacity:.4}\n");DomStyleSlotStageRef c;ok(aether_dom_style_prepare_slot(second->owner_control->binding.lock(),"#x{opacity:.7}\n",c));
    std::vector<DomStyleSlotStageRef> stages{a,c};DomStyleSlotBatchRef batch;const auto before=slot(f.binding);ok(aether_dom_style_finalize_slots(stages,batch));CHECK(!aether_dom_style_validate_slot(a));aether_dom_style_commit_slot(a);CHECK(slot(f.binding).epoch==before.epoch);
    fail_after=0;CHECK(aether_dom_style_validate_batch(batch));aether_dom_style_commit_batch(batch);fail_after=-1;CHECK(slot(f.binding).epoch==before.epoch+1&&slot(b).epoch==before.epoch+1);CHECK(std::fabs(f.opacity()-.7f)<.001f);
    a=f.prepare("#x{opacity:.1}");int64_t foreign=0;ok(aether_style_runtime_add_stylesheet(f.runtime,"#x{opacity:.9}",2,"foreign",&foreign,nullptr));ok(aether_dom_style_prepare_slot(second->owner_control->binding.lock(),"#x{opacity:.6}",c));CHECK(aether_dom_style_finalize_slots({a,c},batch)==AETHER_ERROR_INVALID_STATE);
    bool success=false;for(long i=0;i<2000;++i){a=f.prepare("#x{opacity:.45}");ok(aether_dom_style_prepare_slot(second->owner_control->binding.lock(),"#x{opacity:.65}",c));const auto prior=slot(f.binding);std::vector<DomStyleSlotStageRef> inputs{a,c};fail_after=i;auto status=aether_dom_style_finalize_slots(inputs,batch);fail_after=-1;if(status==AETHER_OK){fail_after=0;CHECK(aether_dom_style_validate_batch(batch));aether_dom_style_commit_batch(batch);fail_after=-1;success=true;break;}CHECK(status==AETHER_ERROR_OUT_OF_MEMORY&&!batch);CHECK(slot(f.binding).epoch==prior.epoch&&slot(b).epoch==prior.epoch);}CHECK(success);CHECK(std::fabs(f.opacity()-.9f)<.001f);
    aether_dom_style_binding_destroy_v1(b);aether_doc_destroy(second);
    // Native removal followed by same-source replacement preserves that removal.
    Fixture replacement("<body><style>#x{opacity:.45}</style><div id='x'></div></body>");
    // The collector joins style elements without a trailing newline.
    a=f.prepare("#x{opacity:.45}");f.publish(a);auto removed=slot(f.binding);CHECK(aether_style_runtime_remove_stylesheet(f.runtime,removed.handle));removed=slot(f.binding);
    aether_dom_style_rebind_t rebind=nullptr;ok(aether_dom_style_binding_prepare_rebind_v1(f.binding,replacement.doc,&rebind));CHECK(slot(f.binding).epoch==removed.epoch);aether_dom_style_binding_t rebound=nullptr;fail_after=0;ok(aether_dom_style_binding_validate_rebind_v1(rebind));ok(aether_dom_style_binding_commit_rebind_v1(rebind,&rebound));fail_after=-1;CHECK(slot(rebound).epoch==removed.epoch&&!slot(rebound).present);Slot invalid{};CHECK(aether_dom_style_binding_read_slot_v1(f.binding,&invalid.handle,&invalid.present,&invalid.epoch)==AETHER_ERROR_INVALID_STATE);aether_dom_style_binding_destroy_v1(f.binding);f.binding=nullptr;CHECK(!replacement.doc->owner_control->binding.expired());aether_dom_style_binding_rebind_destroy_v1(rebind);aether_dom_style_binding_destroy_v1(rebound);
}
static void real_dom_source(){
    Fixture f;f.bind();auto* style=aether_doc_get_element_by_id(f.doc,"s");CHECK(style&&!style->children.empty());auto* text=style->children.front();auto start=slot(f.binding);CHECK(aether_style_runtime_remove_stylesheet(f.runtime,start.handle));start=slot(f.binding);
    aether_dom_edit_spec_v1_t spec{};spec.kind=AETHER_DOM_SET_TEXT;spec.target=text;const char* same="#x{opacity:.2}";spec.value={same,std::strlen(same)};aether_dom_token_v1_t token{};ok(aether_dom_read_token_v1(text,&token));aether_dom_edit_t edit=nullptr;ok(aether_dom_prepare_edit_v1(&token,&spec,&edit));fail_after=0;ok(aether_dom_edit_commit_v1(edit,&token));fail_after=-1;aether_dom_edit_destroy_v1(edit);CHECK(slot(f.binding).epoch==start.epoch&&!slot(f.binding).present);
    const char* changed="#x{opacity:.55}";spec.value={changed,std::strlen(changed)};ok(aether_dom_read_token_v1(text,&token));ok(aether_dom_prepare_edit_v1(&token,&spec,&edit));fail_after=0;ok(aether_dom_edit_commit_v1(edit,&token));fail_after=-1;aether_dom_edit_destroy_v1(edit);CHECK(slot(f.binding).present&&slot(f.binding).epoch==start.epoch+1);CHECK(std::fabs(f.opacity()-.55f)<.001f);
    // Parse is a legacy write and must update the same slot before returning.
    ok(aether_doc_parse_html(f.doc,"<body><style>#x{opacity:.35}</style><div id='x'></div></body>"));CHECK(std::fabs(f.opacity()-.35f)<.001f);
}

static void initial_create_staging(){
    // This is the first successful commit in this process: no warm-up commit.
    Fixture f; aether_dom_style_create_t first=nullptr, second=nullptr, aborted=nullptr;
    ok(aether_dom_style_binding_prepare_create_v1(f.doc,f.runtime,&first));
    ok(aether_dom_style_binding_prepare_create_v1(f.doc,f.runtime,&second));
    ok(aether_dom_style_binding_prepare_create_v1(f.doc,f.runtime,&aborted));
    CHECK(f.doc->owner_control->binding.expired());
    aether_dom_style_binding_create_destroy_v1(aborted);
    ok(aether_dom_style_binding_validate_create_v1(first));
    fault_delivered=false;fail_after=0;
    const auto committed=aether_dom_style_binding_commit_create_v1(first,&f.binding);
    fail_after=-1;CHECK(committed==AETHER_OK&&!fault_delivered);
    CHECK(slot(f.binding).handle==1&&slot(f.binding).epoch==1);
    auto out=reinterpret_cast<aether_dom_style_binding_t>(uintptr_t{1});
    CHECK(aether_dom_style_binding_validate_create_v1(first)==AETHER_ERROR_INVALID_STATE);
    CHECK(aether_dom_style_binding_commit_create_v1(first,&out)==AETHER_ERROR_INVALID_STATE&&!out);
    out=reinterpret_cast<aether_dom_style_binding_t>(uintptr_t{1});
    CHECK(aether_dom_style_binding_commit_create_v1(second,&out)==AETHER_ERROR_INVALID_STATE&&!out);
    aether_dom_style_binding_create_destroy_v1(first);aether_dom_style_binding_create_destroy_v1(second);
    CHECK(slot(f.binding).present&&std::fabs(f.opacity()-.2f)<.001f);
}
static void initial_stale_and_lifetime(){
    for(int kind=0;kind<6;++kind){Fixture f;aether_dom_style_create_t stage=nullptr;ok(aether_dom_style_binding_prepare_create_v1(f.doc,f.runtime,&stage));
        if(kind==0)ok(aether_element_set_attribute(aether_doc_get_element_by_id(f.doc,"x"),"class","changed"));
        if(kind==1)ok(aether_doc_parse_html(f.doc,"<body><style>div{opacity:.7}</style><div></div></body>"));
        if(kind==2)aether_style_runtime_set_diagnostics_callback(f.runtime,nullptr,nullptr);
        if(kind==3){f.bind();aether_dom_style_binding_destroy_v1(f.binding);f.binding=nullptr;}
        if(kind==4){aether_doc_destroy(f.doc);f.doc=nullptr;}
        if(kind==5){aether_style_runtime_destroy(f.runtime);f.runtime=nullptr;}
        auto out=reinterpret_cast<aether_dom_style_binding_t>(uintptr_t{1});
        CHECK(aether_dom_style_binding_validate_create_v1(stage)==AETHER_ERROR_INVALID_STATE);
        CHECK(aether_dom_style_binding_commit_create_v1(stage,&out)==AETHER_ERROR_INVALID_STATE&&!out);
        aether_dom_style_binding_create_destroy_v1(stage);
        if(f.doc)CHECK(f.doc->owner_control->binding.expired());
    }
    Fixture f;aether_dom_style_create_t stage=nullptr;ok(aether_dom_style_binding_prepare_create_v1(f.doc,f.runtime,&stage));
    int result=AETHER_OK;std::thread wrong([&]{result=aether_dom_style_binding_validate_create_v1(stage);});wrong.join();CHECK(result==AETHER_ERROR_INVALID_STATE);
    ok(aether_dom_style_binding_validate_create_v1(stage));aether_dom_style_binding_create_destroy_v1(stage);
}
static void initial_prepare_oom(){
    Fixture f;bool succeeded=false;
    for(long i=0;i<2000;++i){aether_dom_style_create_t stage=reinterpret_cast<aether_dom_style_create_t>(uintptr_t{1});
        aether_dom_token_v1_t before{},after{};ok(aether_dom_read_token_v1(f.doc,&before));
        fault_delivered=false;fail_after=i;auto status=aether_dom_style_binding_prepare_create_v1(f.doc,f.runtime,&stage);fail_after=-1;
        CHECK(f.doc->owner_control->binding.expired());ok(aether_dom_read_token_v1(f.doc,&after));
        CHECK(before.owner_identity==after.owner_identity&&before.observation_serial==after.observation_serial&&before.input_epoch==after.input_epoch&&before.structure_epoch==after.structure_epoch);
        if(status==AETHER_OK){ok(aether_dom_style_binding_commit_create_v1(stage,&f.binding));aether_dom_style_binding_create_destroy_v1(stage);CHECK(slot(f.binding).handle==1&&slot(f.binding).epoch==1);succeeded=true;break;}
        CHECK(status==AETHER_ERROR_OUT_OF_MEMORY&&fault_delivered&&!stage);
    }CHECK(succeeded);
}

int main(){initial_create_staging();initial_stale_and_lifetime();initial_prepare_oom();removal_and_order();identity_and_disposal();diagnostic_replay();fault_atomicity();all_cascade_positions();batch_and_rebind();real_dom_source();
#ifdef AETHER_DOM_STYLE_TESTING
exhaustion();
#endif
std::puts("DOM style binding PASS");}
