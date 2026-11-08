// Linux/glibc integration test: fail the real C allocation entrypoints used by
// parser payloads, in addition to the C++ allocation sweeps in revisions_atomic.
#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#if defined(__has_feature)
# if __has_feature(address_sanitizer)
#  define AETHER_TEST_ADDRESS_SANITIZER 1
# endif
#endif
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__) && !defined(AETHER_TEST_ADDRESS_SANITIZER)
extern "C" void* __libc_malloc(size_t);
extern "C" void* __libc_calloc(size_t,size_t);
namespace {
enum class allocator_e { none, malloc_call, calloc_call };
allocator_e selected = allocator_e::none;
long fail_index = -1, calls = 0;
bool triggered = false;
bool reject(allocator_e kind) noexcept {
    if (selected != kind) return false;
    ++calls;
    if (fail_index >= 0 && fail_index-- == 0) { triggered = true; return true; }
    return false;
}
void arm(allocator_e kind, long index) { calls=0; triggered=false; fail_index=index; selected=kind; }
void disarm() { selected=allocator_e::none; fail_index=-1; }
void need(bool condition,const char* label) {
    if(!condition) { disarm();std::fprintf(stderr,"FAIL malloc/calloc atomic: %s\n",label);std::exit(1); }
}
void ok(aether_status_t status,const char* label) { need(status==AETHER_OK,label); }
}
extern "C" void* malloc(size_t size) noexcept {
    if(reject(allocator_e::malloc_call)) return nullptr;
    return __libc_malloc(size);
}
extern "C" void* calloc(size_t count,size_t size) noexcept {
    if(reject(allocator_e::calloc_call)) return nullptr;
    return __libc_calloc(count,size);
}
namespace {
struct fixture_s {
    aether_doc_t document{};
    aether_style_runtime_t runtime{};
    aether_node_t root{},parent{},child{},multi{},single{};
    explicit fixture_s(bool inline_style = true) {
        ok(aether_doc_create(&document),"create document");
        ok(aether_doc_parse_html(document,"<html><body><div id='p'><span id='c'>text</span><div id='multi'></div><div id='single'></div></div></body></html>"),"parse html");
        aether_style_environment_t env{800,600,1,16,0,0,0};
        ok(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime),"create runtime");
        root=aether_doc_get_document_element(document);
        parent=aether_doc_get_element_by_id(document,"p");child=aether_doc_get_element_by_id(document,"c");
        multi=aether_doc_get_element_by_id(document,"multi");single=aether_doc_get_element_by_id(document,"single");
        // Inline declarations force compile-time validation and application to
        // run inside the same transactional allocation sweep.
        if (inline_style) ok(aether_element_set_attribute(parent,"style","text-shadow:1px 2px red;box-shadow:3px 4px blue;transform:translate(10px,20%) scale(2);--radius:calc(1px /*raw*/ + 2px);backdrop-filter:blur(9px);backdrop-filter:blur(var(--radius)) blur(calc(0px - 1em))"),"set parent style");
        if (inline_style) ok(aether_element_set_attribute(multi,"style","background-image:url(a),linear-gradient(red,blue);background-position:5px 6px"),"set multi image");
        if (inline_style) ok(aether_element_set_attribute(single,"style","background-image:linear-gradient(red,blue)"),"set single gradient");
    }
    ~fixture_s() { aether_style_runtime_destroy(runtime);aether_doc_destroy(document); }
    void verify(aether_style_revision_t revision) {
        unsigned found=0;
        for(int i=0;i<aether_style_revision_style_count(revision);++i) {
            aether_node_t node{}; aether_computed_style_handle_t handle{};uint32_t impact{};
            ok(aether_style_revision_get_style(revision,i,&node,&handle,&impact),"get style");
            aether_computed_style_t style{};ok(aether_computed_style_copy(handle,&style),"copy flat style");
            if(node==parent) {
                ++found;
                need(style.transforms.count==2 && style.transforms.items[0].x.value==10,"valid transform never silently dropped");
                need(style.backdrop_filters.count==2 && style.backdrop_filters.items[0].length_amount.value==3 && style.backdrop_filters.items[1].length_amount.value==0,"valid backdrop never silently dropped");
                need(style.text_shadows.count==1 && style.box_shadows.count==1,"valid shadows never silently dropped");
            } else if(node==child) {
                ++found;need(style.text_shadows.count==1 && style.text_shadows.items[0].offset_x==1,"inherited shadow never silently dropped");
            } else if(node==multi) {
                ++found;need(style.background_layers.count==2,"multi-image payload never silently dropped");
                const auto& first=style.background_layers.items[0];const auto& second=style.background_layers.items[1];
                need(first.image_source && std::strcmp(first.image_source,"a")==0 && second.gradient.stop_count==2,"URL and nested gradient payloads intact");
                need(first.position_x.value==5 && second.position_y.value==6,"background longhands retained");
            } else if(node==single) {
                ++found;need(style.background_layers.count==1 && style.background_layers.items[0].gradient.stop_count==2,"single-gradient calloc applicator never silently dropped");
            }
            aether_computed_style_free(&style);aether_computed_style_handle_destroy(handle);
        }
        need(found==4,"all fixture nodes present");
    }
    void verify_empty(aether_style_revision_t revision) {
        for(int i=0;i<aether_style_revision_style_count(revision);++i) {
            aether_node_t node{};aether_computed_style_handle_t handle{};uint32_t impact{};
            ok(aether_style_revision_get_style(revision,i,&node,&handle,&impact),"get unchanged style");
            aether_computed_style_t style{};ok(aether_computed_style_copy(handle,&style),"copy unchanged style");
            need(style.transforms.count==0 && style.background_layers.count==0 && style.text_shadows.count==0 && style.backdrop_filters.count==0,"failed sheet cannot partially install valid declarations");
            aether_computed_style_free(&style);aether_computed_style_handle_destroy(handle);
        }
    }
    void seed(bool full = true) {
        aether_style_transaction_t transaction{};aether_style_revision_t revision{};
        ok(aether_style_runtime_prepare_revision(runtime,root,nullptr,&transaction),"seed prepare");
        ok(aether_style_runtime_commit_revision(runtime,transaction,&revision),"seed commit");
        aether_style_transaction_destroy(transaction);if(full) verify(revision);else verify_empty(revision);aether_style_revision_destroy(revision);
    }
};
void sweep(allocator_e allocator,const char* name) {
    fixture_s fixture;fixture.seed();
    aether_style_transaction_t warm{};
    arm(allocator,-1);
    const auto warm_status=aether_style_runtime_prepare_revision(fixture.runtime,fixture.root,nullptr,&warm);
    const long allocation_count=calls;
    disarm();ok(warm_status,"count baseline allocations");aether_style_transaction_destroy(warm);
    need(allocation_count>0,"selected C allocator exercised");
    unsigned failed=0,success=0,delivered=0;
    for(long index=0;index<allocation_count+3;++index) {
        aether_style_revision_t before{};ok(aether_style_runtime_retain_revision(fixture.runtime,&before),"retain previous revision");
        const auto generation=aether_style_revision_generation(before);
        aether_style_transaction_t transaction{};
        arm(allocator,index);
        const auto status=aether_style_runtime_prepare_revision(fixture.runtime,fixture.root,nullptr,&transaction);
        const bool did_fail=triggered;
        disarm();delivered+=did_fail;
        if(status==AETHER_OK) {
            ++success;
            aether_style_revision_t current{};ok(aether_style_runtime_commit_revision(fixture.runtime,transaction,&current),"commit successful candidate");
            fixture.verify(current);need(aether_style_revision_generation(current)>generation,"success advances revision");
            aether_style_revision_destroy(current);
        } else {
            ++failed;need(did_fail,"failure corresponds to injected allocation");
            need(status==AETHER_ERROR_OUT_OF_MEMORY,"allocator failure propagates explicit OOM status");
            need(transaction==nullptr,"OOM publishes no candidate");
            aether_style_revision_t current{};ok(aether_style_runtime_retain_revision(fixture.runtime,&current),"retain after failure");
            need(aether_style_revision_generation(current)==generation,"OOM retains previous generation");
            fixture.verify(current);aether_style_revision_destroy(current);
        }
        // Previously retained outputs remain complete in either outcome.
        fixture.verify(before);aether_style_revision_destroy(before);
        aether_style_transaction_destroy(transaction);
    }
    need(failed>0 && success>0 && delivered>=failed,"sweep covered failed and successful preparation");
    std::printf("%s sweep: %ld allocation sites, %u failures, %u successes, zero silent losses\n",name,allocation_count,failed,success);
}
void sweep_add(allocator_e allocator,const char* name) {
    const char* css="#p{text-shadow:1px 2px red;box-shadow:3px 4px blue;transform:translate(10px,20%) scale(2);--radius:calc(1px /*raw*/ + 2px);backdrop-filter:blur(9px);backdrop-filter:blur(var(--radius)) blur(calc(0px - 1em))}#multi{background-image:url(a),linear-gradient(red,blue);background-position:5px 6px}#single{background-image:linear-gradient(red,blue)}";
    long allocation_count=0;
    {
        fixture_s warm(false);int64_t sheet{};
        arm(allocator,-1);
        const auto status=aether_style_runtime_add_stylesheet(warm.runtime,css,0,"allocation-sweep",&sheet,nullptr);
        allocation_count=calls;disarm();ok(status,"baseline add");
    }
    need(allocation_count>0,"sheet parse exercises C allocator");
    unsigned failed=0,success=0;
    for(long index=0;index<allocation_count+3;++index) {
        fixture_s fixture(false);fixture.seed(false);
        aether_style_revision_t before{};ok(aether_style_runtime_retain_revision(fixture.runtime,&before),"retain before sheet");
        const auto generation=aether_style_revision_generation(before);
        int64_t sheet=-123;
        arm(allocator,index);
        const auto status=aether_style_runtime_add_stylesheet(fixture.runtime,css,0,"allocation-sweep",&sheet,nullptr);
        const bool did_fail=triggered;disarm();
        if(status==AETHER_OK) { ++success;fixture.seed(); }
        else {
            ++failed;need(did_fail&&status==AETHER_ERROR_OUT_OF_MEMORY,"sheet allocation failure propagates OOM");
            need(sheet==-123,"failed sheet output unchanged");
            aether_style_revision_t current{};ok(aether_style_runtime_retain_revision(fixture.runtime,&current),"retain after add failure");
            need(aether_style_revision_generation(current)==generation,"failed add preserves revision");
            fixture.verify_empty(current);aether_style_revision_destroy(current);
            fixture.seed(false);
        }
        fixture.verify_empty(before);aether_style_revision_destroy(before);
    }
    need(failed>0&&success>0,"sheet sweep covers failure and success");
    std::printf("%s add sweep: %ld allocation sites, %u failures, %u successes, zero silent losses\n",name,allocation_count,failed,success);
}
}
int main() {
    sweep(allocator_e::malloc_call,"malloc");sweep(allocator_e::calloc_call,"calloc");
    sweep_add(allocator_e::malloc_call,"malloc");sweep_add(allocator_e::calloc_call,"calloc");
}
#else
int main() { std::puts("SKIP: real libc malloc/calloc interposition requires glibc without ASan allocator interposition");return 77; }
#endif
