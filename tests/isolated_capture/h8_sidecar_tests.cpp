#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/css/frozen_measure_read.h"
#include "../../native/src/css/computed_style_handle.h"
#include <aether/aether_contextual_style.h>
#include "../../native/src/style_runtime/style_property_registry_internal.h"
#include <cstring>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <new>
static long budget=-1;
static bool delivered=false;
void* operator new(size_t n) {
    if(budget>=0 && budget--==0){delivered=true;throw std::bad_alloc();}
    if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();
}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}
void operator delete[](void*p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void operator delete(void*p,const std::nothrow_t&)noexcept{std::free(p);}
void operator delete[](void*p,const std::nothrow_t&)noexcept{std::free(p);}
void* operator new(size_t n,std::align_val_t alignment) {
    if(budget>=0 && budget--==0){delivered=true;throw std::bad_alloc();}
    void*p=nullptr;size_t a=static_cast<size_t>(alignment);if(a<sizeof(void*))a=sizeof(void*);
    if(posix_memalign(&p,a,n?n:1))throw std::bad_alloc();return p;
}
void* operator new[](size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void*p,std::align_val_t)noexcept{std::free(p);}
void operator delete[](void*p,std::align_val_t)noexcept{std::free(p);}
void operator delete(void*p,size_t,std::align_val_t)noexcept{std::free(p);}
void operator delete[](void*p,size_t,std::align_val_t)noexcept{std::free(p);}
using namespace aether::style;
static void check(aether_computed_style_handle_t handle) {
    assert(handle && handle->measures);
    assert(handle->measures->intrinsic_keyword("width")==aether::css::intrinsic_size_keyword::fit_content);
    auto flat=aether_computed_style_default();const auto before=flat;
    assert(aether_computed_style_copy(handle,&flat)==AETHER_ERROR_INVALID_STATE);
    assert(!std::memcmp(&flat,&before,sizeof flat));

}
static void literal_revision(aether_style_revision_t revision,aether_node_t target) {
    bool found=false;
    for(int i=0;i<aether_style_revision_style_count(revision);++i){
        aether_node_t node{};aether_computed_style_handle_t style{};
        assert(aether_style_revision_get_style(revision,i,&node,&style,nullptr)==AETHER_OK);
        if(node==target){assert(style->style.width.value==7);assert(style->measures->intrinsic_keyword("width")==aether::css::intrinsic_size_keyword::none);found=true;}
        aether_computed_style_handle_destroy(style);
    }
    assert(found);
}
static size_t runtime_sweep(){
    aether_doc_t doc{};assert(aether_doc_create(&doc)==AETHER_OK);
    assert(aether_doc_parse_html(doc,"<body><div id='target' style='width:7px'></div></body>")==AETHER_OK);
    aether_style_runtime_options_t options{};aether_style_environment_t env{800,600,1,16,0,0,0};
    aether_style_runtime_t runtime{};assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
    aether_style_transaction_t first{};assert(aether_style_runtime_prepare_revision(runtime,aether_doc_get_body(doc),&env,&first)==AETHER_OK);
    aether_style_revision_t old{};assert(aether_style_runtime_commit_revision(runtime,first,&old)==AETHER_OK);aether_style_transaction_destroy(first);
    auto target=aether_doc_get_element_by_id(doc,"target");literal_revision(old,target);
    const auto generation=aether_style_revision_generation(old);
    assert(aether_element_set_attribute(target,"style","width:fit-content")==AETHER_OK);
    size_t failures=0;bool done=false;
    for(long i=0;i<10000;++i){
        aether_style_transaction_t tx=reinterpret_cast<aether_style_transaction_t>(1);delivered=false;budget=i;
        const auto status=aether_style_runtime_prepare_revision(runtime,aether_doc_get_body(doc),&env,&tx);budget=-1;
        // A retained map/revision and the runtime publication must both remain old.
        literal_revision(old,target);
        aether_style_revision_t current{};assert(aether_style_runtime_retain_revision(runtime,&current)==AETHER_OK);
        assert(aether_style_revision_generation(current)==generation);literal_revision(current,target);aether_style_revision_destroy(current);
        if(status==AETHER_OK){
            assert(!delivered&&tx);
            aether_style_revision_t committed{};
            // Publication is allocation-free for this no-diagnostic fixture.
            delivered=false;budget=0;
            const auto committed_status=aether_style_runtime_commit_revision(runtime,tx,&committed);budget=-1;
            assert(committed_status==AETHER_OK&&!delivered);
            assert(aether_style_revision_generation(committed)==generation+1);
            bool found=false;
            for(int j=0;j<aether_style_revision_style_count(committed);++j){
                aether_node_t node{};aether_computed_style_handle_t style{};
                assert(aether_style_revision_get_style(committed,j,&node,&style,nullptr)==AETHER_OK);
                if(node==target){check(style);found=true;}aether_computed_style_handle_destroy(style);
            }
            assert(found);literal_revision(old,target);
            aether_style_revision_destroy(committed);aether_style_transaction_destroy(tx);done=true;break;
        }
        assert(status==AETHER_ERROR_OUT_OF_MEMORY&&delivered&&!tx);++failures;
    }
    assert(done&&failures>100);aether_style_revision_destroy(old);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);
    return failures;
}
extern void builtin_properties_register(style_property_registry_s*);
int main(){
    style_property_registry_s registry;builtin_properties_register(&registry);registry.seal();
    size_t checks=0;
    // Intrinsic basis expansion belongs exclusively to flex, never border.
    // Preserve a preexisting writer as well as the eventual runtime style.
    for (const char* value : {"min-content", "max-content", "fit-content", R"(m\69 n-content)"}) {
        style_shorthand_writer_s output; output.set("sentinel", "retained");
        const auto* border = registry.shorthands.at("border");
        assert(!border->expand(value, output));
        assert(output.items.size()==1 && output.items[0].name=="sentinel" && output.items[0].value=="retained");
        style_shorthand_writer_s flex;
        assert(registry.shorthands.at("flex")->expand(value, flex));
        assert(flex.items.size()==3 && flex.items[2].name=="flex-basis");
        ++checks;
    }

    for (auto name : {"width","min-width","max-width","flex-basis"}) {
        auto* definition=registry.by_name.at(name);
        auto flat=aether_computed_style_default();
        measure_state_s state;state.document=std::make_shared<const document_measure_context_s>();
        std::string property=name;measure_apply_state_s owner(state,property);property_apply_context_s context{&flat,nullptr,&owner};
        for (auto value : {"min-content","MAX-CONTENT","fit-content","m\\69n-content"}) {
            std::string canonical;assert(definition->parse(value,canonical));
            const auto before=flat;assert(!definition->apply_checked(&flat,value).success);assert(!std::memcmp(&flat,&before,sizeof flat));
            assert(definition->apply_with_context_checked(context,value).success);
            assert(owner.read().intrinsic_keyword(property)!=aether::css::intrinsic_size_keyword::none);++checks;
        }
        for (auto value : {"min-content junk","fit-content(10px)","calc-size(min-content,10px)","min-content()"}) {
            std::string canonical;assert(!definition->parse(value,canonical));
            const auto before=flat;const auto tag=owner.read().intrinsic_keyword(property);
            assert(!definition->apply_with_context_checked(context,value).success);assert(!std::memcmp(&flat,&before,sizeof flat));
            assert(owner.read().intrinsic_keyword(property)==tag);++checks;
        }
        assert(definition->apply_with_context_checked(context,"17px").success);
        assert(owner.read().intrinsic_keyword(property)==aether::css::intrinsic_size_keyword::none);
        measure_property_delta_s delta;measure_apply_state_s probe(state,delta,property);property_apply_context_s pc{&flat,nullptr,&probe};
        assert(definition->apply_with_context_checked(pc,"min-content").success);
        assert(probe.read().intrinsic_keyword(property)==aether::css::intrinsic_size_keyword::min_content);
        assert(state.intrinsic_keywords.empty());aether_computed_style_free(&flat);
    }
    // CSS-wide inheritance/replacement, custom property expansion, and both
    // intrinsic flex shorthands travel through the real compiler/runtime path.
    {
        aether_doc_t doc{};assert(aether_doc_create(&doc)==AETHER_OK);
        assert(aether_doc_parse_html(doc,"<body><div id='parent' style='width:fit-content'><div id='child' style='width:inherit;min-width:min-content;max-width:max-content'></div></div><div id='basis' style='flex:min-content'></div><div id='triplet' style='--size:max-content;flex:0 1 var(--size)'></div></body>")==AETHER_OK);
        aether_style_runtime_options_t options{};aether_style_environment_t env{800,600,1,16,0,0,0};
        aether_style_runtime_t runtime{};assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
        auto verify=[&](const char* id,const char* property,aether::css::intrinsic_size_keyword expected) {
            aether_style_transaction_t tx{};assert(aether_style_runtime_prepare_revision(runtime,aether_doc_get_body(doc),&env,&tx)==AETHER_OK);
            aether_style_revision_t revision{};assert(aether_style_runtime_commit_revision(runtime,tx,&revision)==AETHER_OK);aether_style_transaction_destroy(tx);
            bool found=false;
            for(int i=0;i<aether_style_revision_style_count(revision);++i){aether_node_t node{};aether_computed_style_handle_t handle{};
                assert(aether_style_revision_get_style(revision,i,&node,&handle,nullptr)==AETHER_OK);
                if(node==aether_doc_get_element_by_id(doc,id)){assert(handle->measures->intrinsic_keyword(property)==expected);found=true;}
                aether_computed_style_handle_destroy(handle);
            }
            assert(found);aether_style_revision_destroy(revision);++checks;
        };
        using K=aether::css::intrinsic_size_keyword;
        verify("child","width",K::fit_content);verify("child","min-width",K::min_content);verify("child","max-width",K::max_content);
        verify("basis","flex-basis",K::min_content);verify("triplet","flex-basis",K::max_content);
        auto child=aether_doc_get_element_by_id(doc,"child");
        assert(aether_element_set_attribute(child,"style","width:initial;min-width:unset;max-width:initial")==AETHER_OK);
        verify("child","width",K::none);verify("child","min-width",K::none);verify("child","max-width",K::none);
        assert(aether_element_set_attribute(child,"style","width:min-content")==AETHER_OK);verify("child","width",K::min_content);
        assert(aether_element_set_attribute(child,"style","width:17px")==AETHER_OK);verify("child","width",K::none);
        assert(aether_element_set_attribute(child,"style","width:max-content")==AETHER_OK);verify("child","width",K::max_content);
        aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);
    }
    // Invalid border declarations must not override existing flex state when
    // processed by the compiler/runtime, including custom-property expansion.
    for (const char* value : {"min-content", "max-content", "fit-content", "MAX-CONTENT", R"(m\69 n-content)", "var(--bad)"}) {
        aether_doc_t doc{};assert(aether_doc_create(&doc)==AETHER_OK);
        const std::string html=std::string("<body><div id='target' style='--bad:min-content;flex:2 3 17px;border:")+value+"'></div></body>";
        assert(aether_doc_parse_html(doc,html.c_str())==AETHER_OK);
        aether_style_runtime_options_t options{};aether_style_environment_t env{800,600,1,16,0,0,0};
        aether_style_runtime_t runtime{};assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
        aether_style_transaction_t tx{};assert(aether_style_runtime_prepare_revision(runtime,aether_doc_get_body(doc),&env,&tx)==AETHER_OK);
        aether_style_revision_t revision{};assert(aether_style_runtime_commit_revision(runtime,tx,&revision)==AETHER_OK);aether_style_transaction_destroy(tx);
        bool found=false;
        for(int i=0;i<aether_style_revision_style_count(revision);++i){
            aether_node_t node{};aether_computed_style_handle_t handle{};
            assert(aether_style_revision_get_style(revision,i,&node,&handle,nullptr)==AETHER_OK);
            if(node==aether_doc_get_element_by_id(doc,"target")){
                assert(handle->style.flex_grow==2 && handle->style.flex_shrink==3);
                assert(handle->style.flex_basis.length.value==17);
                assert(handle->measures->intrinsic_keyword("flex-basis")==aether::css::intrinsic_size_keyword::none);
                found=true;
            }
            aether_computed_style_handle_destroy(handle);
        }
        assert(found);++checks;
        aether_style_revision_destroy(revision);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);
    }
    size_t freeze_failures=0,clone_failures=0;
    bool completed=false;
    measure_state_s state;
    state.document=std::make_shared<const document_measure_context_s>();
    state.intrinsic_keywords.emplace("width",aether::css::intrinsic_size_keyword::fit_content);
    FrozenDocumentRef document;assert(freeze_document({},document)==AETHER_OK);
    FrozenMeasuresRef original;assert(freeze_runtime_measures(state,document,original)==AETHER_OK);
    completed=false;
    for(long i=0;i<500;++i){
        auto output=original;delivered=false;budget=i;
        const auto status=freeze_runtime_measures(state,document,output);budget=-1;
        if(delivered){++freeze_failures;assert(status==AETHER_ERROR_OUT_OF_MEMORY);assert(output==original);}
        else{assert(status==AETHER_OK);completed=true;break;}
    }
    assert(completed&&freeze_failures>0);
    aether_doc_t doc{};assert(aether_doc_create(&doc)==AETHER_OK);
    assert(aether_doc_parse_html(doc,"<body><div id='target'></div></body>")==AETHER_OK);
    aether_style_runtime_options_t options{};aether_style_environment_t env{800,600,1,16,0,0,0};
    aether_style_runtime_t runtime{};assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
    int64_t sheet{};int32_t rules{};
    assert(aether_style_runtime_add_stylesheet(runtime,"#target{width:fit-content}",0,"h8-owning-update",&sheet,&rules)==AETHER_OK);
    aether_style_update_t* updates{};
    const int count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&updates);assert(count>0);
    auto target=aether_doc_get_element_by_id(doc,"target");aether_style_update_t selected{};
    for(int i=0;i<count;++i){aether_node_t node{};assert(aether_style_runtime_get_update(updates[i],&node,nullptr,nullptr)==AETHER_OK);if(node==target)selected=updates[i];}
    assert(selected);
    aether_computed_style_handle_t before{};assert(aether_style_runtime_get_update(selected,nullptr,&before,nullptr)==AETHER_OK);check(before);
    completed=false;
    for(long i=0;i<500;++i){
        aether_computed_style_handle_t clone{};delivered=false;budget=i;
        const auto status=aether_style_runtime_get_update(selected,nullptr,&clone,nullptr);budget=-1;
        if(delivered){++clone_failures;assert(status==AETHER_ERROR_OUT_OF_MEMORY);assert(!clone);check(before);}
        else{assert(status==AETHER_OK);check(clone);aether_computed_style_handle_destroy(clone);completed=true;break;}
    }
    assert(completed&&clone_failures>0);
    aether_style_runtime_destroy(runtime);
    aether_computed_style_handle_t after{};assert(aether_style_runtime_get_update(selected,nullptr,&after,nullptr)==AETHER_OK);check(after);
    aether_style_updates_free(updates,count);
    check(before);check(after);
    aether_doc_destroy(doc);
    check(before);check(after);
    aether_computed_style_handle_destroy(before);aether_computed_style_handle_destroy(after);
    const auto runtime_failures=runtime_sweep();
    std::printf("PASS H8 runtime prepare OOM=%zu allocation points, atomic commit\n",runtime_failures);
    std::printf("PASS H8 parser checks=%zu; update clone lifetime; OOM atomic freeze=%zu clone=%zu allocation points\n",checks,freeze_failures,clone_failures);
}
