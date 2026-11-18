#ifdef NDEBUG
#undef NDEBUG
#endif
#include <aether/aether_b0_source.h>
#include "../src/rendering/html_ltr/b2_session_provenance.h"
#include "../src/rendering/html_ltr/session.h"
#include "../src/layout/layout_internal.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace aether::rendering::html_ltr;
namespace b2=aether::layout::b2;
static std::string fault;
static size_t events=0,applied=0;
static bool hit=false;
static B2Translation saved{};static B2TranslationSink* saved_sink=nullptr;
static aether_b0_source_options_v1 options;
namespace aether::rendering::html_ltr::testing {
void stage(Stage){}
void b2_result_inspect(B2InlineResult&){}
void b2_inspect(b2::Ledger&){}
void inspect(aether_layout_result_s* layout,aether::rendering::NativePaintList* paint){
 assert(!layout->b2_translation_sink);
 if(!paint&&fault=="late-event"){assert(saved_sink);hit=true;saved_sink->translate(saved);}
 if(paint||fault.rfind("final-",0)!=0)return;
 std::vector<aether_layout_box_s*> todo{layout->root};
 while(!todo.empty()) {auto* b=todo.back();todo.pop_back();
  if(b->kind==LAYOUT_BOX_TEXT){hit=true;
   if(fault=="final-x")b->content_box.x+=1;
   else if(fault=="final-y")b->content_box.y+=1;
   else if(fault=="final-width")b->content_box.width+=1;
   else if(fault=="final-height")b->content_box.height+=1;
   else if(fault=="final-baseline")b->native_text.baseline+=1;
   else if(fault=="final-text")b->text="changed";
   else if(fault=="final-run")b->native_text.run.reset();
   else if(fault=="final-font") {const auto old=b->native_text.run;auto font=std::make_shared<const text::FontInstance>(old->font->source,old->font->face_index,old->font->size_26_6+64,old->font->raster_flags);b->native_text.run=std::make_shared<const text::ShapedRun>(font,old->source,old->glyphs,old->metrics);}
   else if(fault=="final-owner")b->element=layout->root->element;
   else if(fault=="final-style")b->style=layout->root->style;
   else if(fault=="final-omitted")b->kind=LAYOUT_BOX_ELEMENT;
   else if(fault=="final-duplicate") {auto* extra=layout->create_box();extra->kind=LAYOUT_BOX_TEXT;extra->element=b->element;extra->style=b->style;extra->text=b->text;extra->native_text=b->native_text;extra->content_box=b->content_box;layout->root->add_child(extra);}
   else assert(false);
   return;
  }for(auto* c:b->children)todo.push_back(c);
 }
 assert(false);
}
}
static void exhaust(B2TranslationSink* sink){
 auto* provenance=dynamic_cast<B2Provenance*>(sink);assert(provenance);
 auto& counters=provenance->counters();
 counters.charge(b2::Quota::token_visits,b2::limits[size_t(b2::Quota::token_visits)]-counters.used(b2::Quota::token_visits));
 assert(counters.used(b2::Quota::token_visits)==b2::limits[size_t(b2::Quota::token_visits)]);
}
static bool event(B2TranslationSink* sink,B2Translation& e){
 ++events;saved=e;saved_sink=sink;
 if(fault=="oom-before"&&events==1){hit=true;throw std::bad_alloc();}
 if(fault=="oom-partial"&&events==2){hit=true;assert(applied==1);throw std::bad_alloc();}
 if(fault=="limit-before"&&events==1){hit=true;exhaust(sink);}
 if(fault=="limit-partial"&&events==2){hit=true;assert(applied==1);exhaust(sink);}
 if(fault.rfind("event-",0)!=0||hit)return true;hit=true;
 if(fault=="event-overflow"){
  e.dx=std::numeric_limits<float>::max();sink->translate(e);
  e.before.x+=e.dx;e.before.y+=e.dy;e.operation=sink->operation(e.layout);
  // Second authorized operation would overflow expected x to infinity.
 }
 if(fault=="event-omitted")return false;
 if(fault=="event-duplicate")sink->translate(e);
 else if(fault=="event-order")e.operation=0;
 else if(fault=="event-future")++e.operation;
 else if(fault=="event-session")e.session=nullptr;
 else if(fault=="event-layout")e.layout=nullptr;
 else if(fault=="event-run")e.run=nullptr;
 else if(fault=="event-owner")e.owner=e.layout->root?e.layout->root->element:nullptr;
 else if(fault=="event-style")e.style=nullptr;
 else if(fault=="event-before")e.before.y+=1;
 else if(fault=="event-width")e.before.width+=1;
 else if(fault=="event-height")e.before.height+=1;
 else if(fault=="event-baseline")e.baseline+=1;
 else if(fault=="event-zero")e.dx=e.dy=0;
 else if(fault=="event-nan")e.dx=NAN;
 else if(fault=="event-inf")e.dy=INFINITY;
 else if(fault=="event-without-move")e.dy+=1;
 return true;
}
static void after(B2TranslationSink* sink,const B2Translation&){++applied;if(fault=="oom-after"&&applied==1){hit=true;throw std::bad_alloc();}if(fault=="limit-after"&&applied==1){hit=true;exhaust(sink);}}
static void operation(uint64_t& id){if(fault=="operation-overflow"){id=UINT64_MAX;hit=true;}}
static const std::string html="<html><head><style>body{white-space:normal}p{margin-bottom:16px}</style></head><body><p>A</p><p>same <span>same</span><strong> same</strong></p><p>end</p></body></html>";
static void run(int expected){
 events=applied=0;hit=false;aether_b0_source_t s=reinterpret_cast<aether_b0_source_t>(1);aether_b0_source_diagnostic_v1 d{};d.struct_size=sizeof d;d.version=1;
 const auto status=aether_b0_source_create(320,480,html.data(),html.size(),&options,&s,&d);
 if(status!=expected)fprintf(stderr,"fault=%s expected=%d actual=%d stage=%u reason=%u events=%zu\n",fault.c_str(),expected,status,d.stage,d.reason,events);
 assert(status==expected);if(expected==AETHER_OK){assert(s&&events>0);assert(aether_b0_source_render(s,&d)==AETHER_OK);aether_b0_source_frame_info_v1 info{};info.struct_size=sizeof info;info.version=1;assert(aether_b0_source_copy_frame(s,&info,nullptr,0)==AETHER_OK&&info.byte_count);std::vector<uint8_t> frame(info.byte_count);assert(aether_b0_source_copy_frame(s,&info,frame.data(),frame.size())==AETHER_OK);assert(aether_b0_source_destroy(s)==AETHER_OK);}else {assert(hit&&!s&&d.stage==AETHER_B0_STAGE_LAYOUT);if(expected==AETHER_ERROR_LIMIT_EXCEEDED)assert(d.reason==AETHER_B0_REASON_LIMIT);if(expected==AETHER_ERROR_INTERNAL)assert(d.reason==AETHER_B0_REASON_INVARIANT);}
}
static std::vector<uint8_t> read(const char* p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv){assert(argc==3);auto r=read(argv[1]),b=read(argv[2]);options.struct_size=sizeof options;options.version=1;options.regular_font=r.data();options.regular_font_size=r.size();options.bold_font=b.data();options.bold_font_size=b.size();testing::translation_event=event;testing::translation_applied=after;testing::translation_operation=operation;
 run(AETHER_OK);
 for(const char* f:{"event-overflow","late-event","event-omitted","event-duplicate","event-order","event-future","event-session","event-layout","event-run","event-owner","event-style","event-before","event-width","event-height","event-baseline","event-zero","event-nan","event-inf","event-without-move","operation-overflow","final-x","final-y","final-width","final-height","final-baseline","final-text","final-run","final-font","final-owner","final-style","final-omitted","final-duplicate","oom-before","oom-partial","oom-after","limit-before","limit-partial","limit-after"}){
  fault=f;const auto expected=fault.rfind("oom-",0)==0?AETHER_ERROR_OUT_OF_MEMORY:fault.rfind("limit-",0)==0?AETHER_ERROR_LIMIT_EXCEEDED:AETHER_ERROR_INTERNAL;
  run(expected);run(expected);fault.clear();run(AETHER_OK);
 }
 puts("B2 translation corruption, ordered receipts, OOM/limit partial rollback and retry PASS (38 faults twice + clean sessions)");
}
