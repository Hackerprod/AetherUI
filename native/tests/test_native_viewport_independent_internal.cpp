#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/native_viewport_capture.h"
#include "../src/rendering/display_list_types.h"
#include "../src/layout/layout_internal.h"
#include "../src/css/style_map_types.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>
static long fail_after=-1;static std::size_t allocations=0;
void* operator new(std::size_t n){++allocations;if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&)noexcept{::operator delete(p);}
namespace aether::layout {class RealHtmlTextTestAccess {public:static auto create(aether_doc_s*d,aether_layout_result_s*l,aether_style_map_s*m){return aether::rendering::NativeViewportAuthority::create(d,l,m,100,80,{});}};}
namespace nr=aether::rendering;
using A=nr::NativeViewportAuthority;using E=nr::text::truetype::HtmlTextError;using Access=aether::layout::RealHtmlTextTestAccess;
struct Context{
 aether_doc_t d{};aether_style_runtime_t rt{};aether_style_transaction_t tx{};aether_style_revision_t rev{};aether_style_map_t m{};aether_layout_engine_t en{};aether_layout_result_t l{};
 explicit Context(const char* child="<div style='display:block;width:200px;height:200px;background-color:red'></div>"){
 assert(aether_doc_create(&d)==AETHER_OK);std::string raw="<html><head></head><body style='margin:0px;padding:0px;border-width:0px;width:auto;overflow:auto'>";raw+=child;raw+="</body></html>";assert(aether_doc_parse_html(d,raw.c_str())==AETHER_OK);
 aether_style_environment_t env{100,80,1,16,0,0,0};assert(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&rt)==AETHER_OK);assert(aether_style_runtime_prepare_revision(rt,d,&env,&tx)==AETHER_OK);assert(aether_style_runtime_commit_revision(rt,tx,&rev)==AETHER_OK);assert(aether_style_revision_create_map(rev,&m)==AETHER_OK);assert(aether_layout_create(nullptr,nullptr,&en)==AETHER_OK);assert(aether_layout_layout(en,d,m,100,80,nullptr,nullptr,&l)==AETHER_OK);
 }
 ~Context(){aether_layout_result_destroy(l);aether_layout_destroy(en);aether_style_map_destroy(m);aether_style_revision_destroy(rev);aether_style_transaction_destroy(tx);aether_style_runtime_destroy(rt);aether_doc_destroy(d);}
};
static void same(const aether_display_list_builder_s&a,const aether_display_list_builder_s&b){
#define EQ(x) assert(a.x==b.x)
 EQ(native_viewport_capture);EQ(native_image_source_bytes);EQ(native_image_catalog);EQ(native_images);EQ(native_gradient_output);EQ(opacity_events);EQ(native_text_runs);EQ(native_scroll_depth);EQ(opacity_parents);EQ(opacity_serial);EQ(border_groups);EQ(contextual_text_measurer);EQ(contextual_text_measurer_data);EQ(text_measurer);EQ(text_measurer_data);EQ(image_dimensions);EQ(image_dimensions_data);EQ(viewport.width);EQ(viewport.height);EQ(root_font_size);EQ(style_pin);EQ(canvas_background_source);EQ(transition_runtime);EQ(layout);
#undef EQ
}
static A make(Context&c){auto r=Access::create(c.d,c.l,c.m);assert(std::holds_alternative<A>(r));return std::move(std::get<A>(r));}
int main(){unsigned checks=0;Context c;auto authority=make(c);assert(authority.accepts(c.l,c.m));++checks;
 {Context foreign;assert(!authority.accepts(foreign.l,c.m));assert(std::holds_alternative<E>(Access::create(c.d,foreign.l,c.m)));checks+=2;}
 auto copied=aether_style_map_snapshot(c.m);assert(!authority.accepts(c.l,copied.get()));++checks;
 bool answer=true;std::thread other([&]{answer=authority.accepts(c.l,c.m);});other.join();assert(!answer);++checks;
 for(auto p:{&c.d->identity->structure_epoch,&c.d->identity->mutation_epoch,&c.d->identity->style_tree_epoch,&c.d->identity->non_state_epoch}){++*p;assert(!authority.accepts(c.l,c.m));--*p;assert(authority.accepts(c.l,c.m));++checks;}
 auto* root=c.l->root;assert(!root->children.empty());auto* child=root->children.front();c.l->root=child;assert(!authority.accepts(c.l,c.m));assert(std::holds_alternative<E>(Access::create(c.d,c.l,c.m)));c.l->root=root;++checks;
 for(auto p:{&c.l->viewport.width,&c.l->viewport.height,&root->border_box.x,&root->border_box.y,&root->padding_box.x,&root->padding_box.y,&root->content_box.x,&root->content_box.y}){auto value=*p;*p+=1;assert(!authority.accepts(c.l,c.m));*p=value;assert(authority.accepts(c.l,c.m));++checks;}
 for(auto p:{&root->scroll.scrollable_overflow_x,&root->scroll.scrollable_overflow_y}){auto value=*p;*p+=1;assert(!authority.accepts(c.l,c.m));*p=value;assert(authority.accepts(c.l,c.m));++checks;}
 auto id=root->element->style_node_id;root->element->style_node_id+=1;assert(!authority.accepts(c.l,c.m));root->element->style_node_id=id;++checks;
 {Context nested("<div style='display:block;width:200px;height:200px;overflow:auto'><div style='display:block;width:400px;height:400px'></div></div>");assert(std::holds_alternative<E>(Access::create(nested.d,nested.l,nested.m)));++checks;}
 aether_display_list_builder_t b{};assert(aether_display_list_builder_create(nullptr,nullptr,&b)==AETHER_OK);
 b->native_image_source_bytes=17;b->native_scroll_depth=3;b->opacity_parents={91,92,93};b->opacity_serial=29;b->viewport={7,9};b->root_font_size=27;b->layout=c.l;b->canvas_background_source=root->element;b->text_measurer_data=&checks;b->image_dimensions_data=&checks;b->contextual_text_measurer_data=&checks;
 nr::NativeImageCatalog images;std::vector<nr::NativeImageEntry> prior_images;std::vector<nr::NativeTextEntry> prior_runs;std::vector<aether_border_group_range> prior_borders;aether_display_list_s prior_gradient;
 b->native_image_catalog=&images;b->native_images=&prior_images;b->native_text_runs=&prior_runs;b->border_groups=&prior_borders;b->native_gradient_output=&prior_gradient;
 auto saved=*b;std::unique_ptr<nr::NativePaintList> output;assert(nr::build_native_viewport_capture(b,c.l,c.m,authority,images,output)==AETHER_OK);same(*b,saved);auto* previous=output.get();assert(previous);auto commands=nr::NativeListAccess::list(*output)->commands.size();++checks;
 {int clips=0,scroll=0;unsigned pushes=0,chrome=0;for(auto&cmd:nr::NativeListAccess::list(*output)->commands){if(cmd.tag==AETHER_PAINT_PUSH_CLIP)++clips;if(cmd.tag==AETHER_PAINT_POP_CLIP){assert(clips>0);--clips;}if(cmd.tag==AETHER_PAINT_PUSH_SCROLL_OFFSET){assert(scroll==0&&clips==2);++scroll;++pushes;}if(cmd.tag==AETHER_PAINT_POP_SCROLL_OFFSET){assert(scroll==1&&clips==2);--scroll;}if(cmd.tag==AETHER_PAINT_DRAW_SCROLLBAR_CHROME){assert(scroll==0&&clips==1);++chrome;}}assert(clips==0&&scroll==0&&pushes==1&&chrome==1);++checks;}

 auto legacy=[&]{std::unique_ptr<nr::NativePaintList> rejected;assert(nr::build_native_opacity(b,c.l,c.m,nullptr,nr::NativeScrollInput{},images,rejected)==AETHER_ERROR_INVALID_STATE);assert(!rejected);*b=saved;};legacy();
 allocations=0;assert(nr::build_native_viewport_capture(b,c.l,c.m,authority,images,output)==AETHER_OK);const auto sites=allocations;same(*b,saved);previous=output.get();commands=nr::NativeListAccess::list(*output)->commands.size();unsigned ooms=0;
 for(std::size_t i=0;i<sites;++i){fail_after=i;auto status=nr::build_native_viewport_capture(b,c.l,c.m,authority,images,output);fail_after=-1;assert(status==AETHER_ERROR_OUT_OF_MEMORY);++ooms;same(*b,saved);assert(output.get()==previous&&nr::NativeListAccess::list(*output)->commands.size()==commands);legacy();}
 {Context empty("");auto no=make(empty);assert(!no.scrolling());assert(nr::build_native_viewport_capture(b,empty.l,empty.m,no,images,output)==AETHER_OK);same(*b,saved);++checks;}
 b->native_viewport_capture=&authority;auto active=*b;assert(nr::build_native_viewport_capture(b,c.l,c.m,authority,images,output)==AETHER_ERROR_INVALID_STATE);same(*b,active);*b=saved;++checks;
 aether_doc_destroy(c.d);c.d=nullptr;assert(!authority.accepts(c.l,c.m));assert(nr::build_native_viewport_capture(b,c.l,c.m,authority,images,output)==AETHER_ERROR_INVALID_STATE);same(*b,saved);++checks;
 aether_display_list_builder_destroy(b);
 std::cout<<"Q2g independent internal checks="<<checks<<" observed_scalar_allocation_sites="<<sites<<" typed_oom="<<ooms<<" all_builder_fields_restored=yes last_output_preserved=yes legacy_root_rejection=yes builder_cancel=NOT_APPLICABLE(no_api)\n";
}
