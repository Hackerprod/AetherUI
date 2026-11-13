#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/rendering/text/native_inline_paragraph.h"
#include <cassert>
#include <iostream>
#include <cstdlib>
#include <new>
static long fail_after=-1;
void* operator new(std::size_t n) {
 if(fail_after==0) throw std::bad_alloc();
 if(fail_after>0) --fail_after;
 if(void* p=std::malloc(n?n:1)) return p;
 throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
using namespace aether::rendering::text;
int main() {
 int checks=0;
 auto ck=[&](bool b){assert(b);++checks;};
 auto bad=[&](auto fn){bool rejected=false;try{fn();}catch(const std::invalid_argument&){rejected=true;}ck(rejected);};
 auto font=std::make_shared<const FontInstance>(std::vector<uint8_t>{1,2,3},"SYNTHETIC_T17_NOT_FONT",0,16*64,0);
 auto seal=std::make_shared<const SyntheticFaceSeal>(font,"SyntheticTestNonAdditive");
 auto para=[&](std::vector<std::string> p){return std::make_shared<const NativeInlineParagraph>(p,seal);};
 auto measured=[&](auto p,std::vector<Glyph> g,float width){
  auto run=std::make_shared<const ShapedRun>(font,p->text,std::move(g),aether_text_metrics_t{width,16,12},p);
  return ContextualMeasurement{p,0,p->text.size(),{{width,16,12},run}};
 };
 for(auto pair:std::vector<std::pair<std::string,std::string>>{{"A","V"},{"T","o"},{"f","fi"}}) {
  auto whole=para({pair.first+pair.second}), split=para({pair.first,"",pair.second});
  ck(whole->text==split->text);ck(split->source_map.size()==split->text.size()+1);
  auto r=measured(split,{{123,0,7*64,0,0,0}},7);
  validate_contextual_measurement(split,r);ck(r.measured.run->glyphs.size()==1);
  bad([&]{validate_contextual_measurement(whole,r);});
 }
 auto p=para({" A ","  V "});ck(p->text=="A V");
 ck(p->source_map==std::vector<SourceMapRange>{{0,0,1,0,0},{0,1,1,0,1},{0,2,1,1,1},
  {1,0,1,1,1},{1,1,1,1,1},{1,2,1,2,1},{1,3,1,3,0}});
 // Independent normalization oracle and every two-piece boundary, including
 // boundaries inside a collapsed space. Provenance reconstructs each output.
 for(unsigned code=0;code<243;++code) {
  unsigned v=code;std::string raw;
  for(int i=0;i<5;++i){raw+=" A~"[v%3];v/=3;}
  std::string expected;bool pending=false;
  for(char c:raw) {if(c==' '){pending=!expected.empty();continue;}if(pending)expected+=' ';pending=false;expected+=c;}
  for(size_t split=0;split<=raw.size();++split) {
   std::vector<std::string> pieces{raw.substr(0,split),"",raw.substr(split)};
   auto q=para(pieces);ck(q->text==expected);
   std::vector<bool> covered(q->text.size(),false);
   for(const auto& m:q->source_map) {
    ck(m.piece<pieces.size());ck(m.source_begin+m.source_length<=pieces[m.piece].size());
    ck(m.output_begin+m.output_length<=q->text.size());
    if(m.output_length) {ck(m.output_length==1);ck(pieces[m.piece][m.source_begin]==q->text[m.output_begin]);covered[m.output_begin]=true;}
   }
   for(bool c:covered)ck(c);
  }
 }
 auto empty=para({"","A","","V",""});ck(empty->text=="AV");
 ck(empty->source_map==std::vector<SourceMapRange>{{0,0,0,0,0},{1,0,1,0,1},{2,0,0,1,0},{3,0,1,1,1},{4,0,0,2,0}});
 auto internal=para({"A ","","  V"});ck(internal->text=="A V");
 ck(internal->source_map[2]==SourceMapRange{1,0,0,1,0});
 auto spaces=para({" ","","  "});ck(spaces->text.empty());
 for(const auto& r:spaces->source_map){ck(r.output_begin==0);ck(r.output_length==0);}
 ck(para({})->text.empty());ck(para({""})->source_map.size()==1);
 for(std::string s:std::vector<std::string>{"\t","\n","\r","\f",std::string(1,'\0'),std::string(1,char(0x80)),std::string(1,char(0x7f))})
  bad([&]{para({"A",s,"V"});});
 bad([&]{para({std::string(max_text_bytes+1,'a')});});
 bad([&]{NativeInlineParagraph n({"A"},{});});
 bad([&]{SyntheticFaceSeal s(font,"", "en","Latn","default");});
 auto av=para({"A","V"});
 auto valid=measured(av,{{1,0,3*64,0,0,0},{2,1,4*64,0,0,0}},7);
 validate_contextual_measurement(av,valid);ck(valid.measured.metrics.width==7);
 auto repeated=measured(av,{{1,0,64,0,0,0},{2,0,64,0,0,0}},2);
 validate_contextual_measurement(av,repeated);ck(repeated.measured.run->glyphs.size()==2);
 auto invalid=valid;invalid.begin=1;bad([&]{validate_contextual_measurement(av,invalid);});
 invalid=valid;invalid.end=1;bad([&]{validate_contextual_measurement(av,invalid);});
 invalid=valid;invalid.measured.metrics.baseline=11;bad([&]{validate_contextual_measurement(av,invalid);});
 bad([&]{validate_contextual_measurement(av,measured(av,{{1,1,64,0,0,0}},1));});
 auto abc=para({"abc"});
 bad([&]{validate_contextual_measurement(abc,measured(abc,{{1,0,64,0,0,0},{2,2,64,0,0,0},{3,1,64,0,0,0}},3));});
 bad([&]{validate_contextual_measurement(av,measured(av,{{1,0,-64,0,0,0}},0));});
 bad([&]{validate_contextual_measurement(av,measured(av,{{1,0,64,1,0,0}},1));});
 bad([&]{validate_contextual_measurement(av,measured(av,{{1,0,64,0,0,0}},2));});
 bad([&]{validate_contextual_measurement(av,measured(av,{{1,0,65536*64,0,0,0},{2,1,64,0,0,0}},65536));});
 // Fail every allocation in immutable paragraph construction; an existing
 // published paragraph remains usable and a later retry succeeds.
 const std::vector<std::string> oom_pieces{std::string(100,'A'),"  ","",std::string(100,'V')};
 int failures=0;bool completed=false;
 for(long at=0;at<100;++at) {
  fail_after=at;
  try {auto q=std::make_shared<const NativeInlineParagraph>(oom_pieces,seal);fail_after=-1;ck(q->text.size()==201);completed=true;break;}
  catch(const std::bad_alloc&){fail_after=-1;++failures;ck(valid.measured.run->source=="AV");}
 }
 fail_after=-1;ck(completed);ck(failures>0);
 // Metadata survives its source vector/font/provider owners without a runtime.
 auto retained=valid.measured.run;av.reset();valid={};seal.reset();font.reset();
 ck(retained->paragraph->text=="AV");ck(retained->paragraph->seal->fixture_id=="SyntheticTestNonAdditive");
 std::cout<<"T1.7 paragraph values PASS checks="<<checks<<" OOM="<<failures<<"; synthetic structure only; real shaping/PNG/Chromium NOT_RUN\n";
}
