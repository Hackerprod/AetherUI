// Private reverse-placement lifetime and percentage-height contracts.
// Core object linkage makes allocation injection observe the transient plan.
#include "../src/layout/layout_internal.h"
#include <new>
#include <cstdio>
#include <cstdlib>
// Link actual core objects into this executable, never the shared library.
// Fault injection must intercept core allocations as well as test allocations.
static long fail_at = -1;
static long allocation_count = 0;
static bool injected = false;
// A bounded, allocation-free ledger observes C++ new/delete, not malloc.
static bool tracking = false;
struct Allocation { void* pointer; size_t bytes; };
static Allocation ledger[65536]{};
static size_t live_allocations = 0, live_bytes = 0, ledger_end = 0;
static void release(void* p) noexcept {
    if (!p) return;
    for (size_t i = 0; i < ledger_end; ++i) if (ledger[i].pointer == p) {
        --live_allocations; live_bytes -= ledger[i].bytes; ledger[i] = {};
        while (ledger_end && !ledger[ledger_end - 1].pointer) --ledger_end;
        break;
    }
    std::free(p);
}
void* operator new(std::size_t size) {
    const auto ordinal = allocation_count++;
    if (fail_at >= 0 && ordinal == fail_at) {
        injected = true;
        throw std::bad_alloc();
    }
    if (auto* p = std::malloc(size ? size : 1)) {
        if (tracking) {
            size_t i = 0;
            while (i < ledger_end && ledger[i].pointer) ++i;
            if (i == 65536) std::abort();
            ledger[i] = {p, size};
            if (i == ledger_end) ++ledger_end;
            ++live_allocations; live_bytes += size;
        }
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { release(p); }


// Share only test fixtures/check helpers. The public matrix has its own target.
#define main aether_reverse_public_matrix_main
#include "test_flex_reverse.cpp"
#undef main

namespace {
void forced_contract(bool prepared) {
 for(bool col:{false,true}) {
 label=col?"forced column reverse height":"forced row reverse height";
 Fixture f(std::string("#p{display:flex;flex-direction:")+(col?"column-reverse":"row-reverse")+";width:100px;height:auto;min-height:0;align-items:flex-start}#a{flex:none;width:40px;height:"+(col?"40px":"50%")+";min-height:0}#b,#c{display:none}#d{height:20px}",prepared);
 auto source=f.layout();
 StyleLookup styles(source->assets,{},false);
 for(auto kind:{forced_height_kind_e::definite,forced_height_kind_e::indefinite}) {
  auto output=new aether_layout_result_s();output->assets=source->assets;output->viewport={640,480};
  output->build_overlay=std::make_unique<aether::layout::MutableLayoutOverlay>();
  auto box=layout_element(f.engine,&styles,f.node("p"),{0,0,100,480},std::nullopt,0,true,100,true,100,kind,true,output);
  output->root=box;auto child=output->primary_boxes.at(f.node("a"));
  eq(box->content_box.height,100,"numeric forced height");
  float expected=col?40:kind==forced_height_kind_e::definite?50:20;
  eq(child->border_box.height,expected,"forced numeric height is not percentage definiteness");
  eq(col?child->border_box.y-box->content_box.y:child->border_box.x-box->content_box.x,col?100-expected:60,"forced reverse final anchor");
  aether_layout_result_destroy(output);
 }
 aether_layout_result_destroy(source);
 }
}
size_t reachable(aether_layout_box_s* root,aether_layout_box_s* needle) {
 size_t n=root==needle;for(auto child:root->children)n+=reachable(child,needle);return n;
}
void owned(aether_layout_result_t frame) {
 need(frame->records_boxes&&!frame->abandoned&&!frame->build_overlay,"published complete frame");
 size_t count=0;for(auto box:frame->allocated_boxes){need(box->owner==frame&&reachable(frame->root,box)==1,"single box owner");++count;}
 need(count==frame->live_box_count,"registry count");
 for(auto box:frame->paint_order)need(reachable(frame->root,box)==1,"paint box reachable");
 for(auto& entry:frame->primary_boxes)need(reachable(frame->root,entry.second)==1&&entry.second->element==entry.first,"primary reachable");
}
struct SavedBox {aether_layout_box_s* box;aether_rectf border,margin,content;std::vector<aether_layout_box_s*> children;};
void unchanged(const std::vector<SavedBox>& saved) {
 for(const auto& s:saved){need(std::memcmp(&s.box->border_box,&s.border,sizeof(s.border))==0,"retained border unchanged");need(std::memcmp(&s.box->margin_box,&s.margin,sizeof(s.margin))==0,"retained margin unchanged");need(std::memcmp(&s.box->content_box,&s.content,sizeof(s.content))==0,"retained content unchanged");need(s.box->children==s.children,"retained child order unchanged");}
}
long oom_failures=0,clone_failures=0;
void allocation_contract(bool prepared,bool col,bool wrap) {
 label=std::string("reverse allocation/")+(col?"column/":"row/")+(wrap?"wrap/":"nowrap/")+(prepared?"prepared":"legacy");
 const char* html="<body><div id='p'> X <div id='a'><div id='d'>nested text</div></div><div id='b'></div><div id='c'></div><div id='abs'></div></div></body>";
 Fixture f(setup(col,col?"column-reverse":"row-reverse","flex-start",100)+"#p{flex-wrap:"+(wrap?"wrap":"nowrap")+"}#a{position:relative;left:2px;top:3px}#b{margin-left:auto}#abs{position:absolute;width:13px;height:17px}",prepared,html);
 auto source=f.layout();owned(source);
 auto assets=source->assets;auto primary=source->primary_boxes;auto order=source->paint_order;
 auto map=f.map->current;auto epoch=f.doc->identity->structure_epoch;auto mutation=f.doc->identity->mutation_epoch;
 std::vector<SavedBox> saved;for(auto b:source->allocated_boxes)saved.push_back({b,b->border_box,b->margin_box,b->content_box,b->children});
 for(bool clone:{false,true}) {
  bool complete=false;
  for(long ordinal=0;ordinal<20000;++ordinal) {
   need(live_allocations==0&&live_bytes==0,"empty allocation ledger");
   auto output=source;allocation_count=0;injected=false;tracking=true;fail_at=ordinal;
   auto status=clone?aether_layout_clone_snapshot(source,&output):aether_layout_layout(f.engine,f.doc,f.map,640,480,nullptr,nullptr,&output);
   fail_at=-1;tracking=false;
   need(f.map->current==map&&source->assets==assets,"map and assets unchanged");need(f.doc->identity->structure_epoch==epoch&&f.doc->identity->mutation_epoch==mutation,"DOM epochs unchanged");
   need(source->primary_boxes==primary&&source->paint_order==order,"retained indexes unchanged");unchanged(saved);
   if(status==AETHER_OK) {need(!injected&&output!=source,"successful complete operation");owned(output);aether_layout_result_destroy(output);complete=true;}
   else {need(injected&&status==AETHER_ERROR_OUT_OF_MEMORY&&output==source,"atomic OOM leaves output untouched");(clone?clone_failures:oom_failures)++;}
   need(live_allocations==0&&live_bytes==0,"no leaked transient plan or tree allocations");
   if(complete)break;
  }
  need(complete,"bounded OOM sweep reached success");
 }
 aether_layout_result_t clone=nullptr;need(aether_layout_clone_snapshot(source,&clone)==AETHER_OK,"retained clone");
 aether_layout_result_destroy(source);
 // Release runtime/map first. Layout assets retain snapshots, not mutable owners.
 aether_style_map_destroy(f.map);f.map=nullptr;aether_style_runtime_destroy(f.runtime);f.runtime=nullptr;
 owned(clone);auto a=f.border(clone,"a");check(std::isfinite(a.x)&&std::isfinite(a.y),"clone survives map/runtime/source destruction");
 aether_layout_result_destroy(clone);
}
}
int main(){for(bool prepared:{false,true}){forced_contract(prepared);for(bool col:{false,true})for(bool wrap:{false,true})allocation_contract(prepared,col,wrap);}std::printf("reverse private: %ld layout OOM, %ld clone OOM; %d checks, %d failures\n",oom_failures,clone_failures,checks,failures);return failures?1:0;}
