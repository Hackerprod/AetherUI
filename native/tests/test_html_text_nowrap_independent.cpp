// Reuse ONLY Q2c synthetic-font writer, binding and fault-injection harness.
// Its original test main is retained as a callable regression, not executed here.
#define main q2c_regression_main
#include "test_html_text_real_independent.cpp"
#undef main
#include "html_text_nowrap_cases.h"
#include "../src/rendering/text/real_html_normalization.h"
static std::string nowrap_html(const std::string& text){auto css=q2c_cases::style();auto p=css.find("white-space:pre");assert(p!=std::string::npos);css.replace(p,15,"white-space:nowrap");return q2c_cases::document(q2c_cases::owner(text,css));}
static HF nowrap_ok(const std::string& raw,Binding b){return ok(nowrap_html(raw),b);}
static void bad_nowrap(const std::string& raw,Binding b){assert(std::holds_alternative<tt::HtmlTextError>(tt::render_real_html_text(nowrap_html(raw),b,{3072,4096},256,128)));++fixtures;}
static void geometry_equal(HF a,HF b){assert(a->frame.pixels==b->frame.pixels);assert(a->run->width_26_6==b->run->width_26_6);assert(a->boxes.size()==b->boxes.size());for(unsigned i=0;i<a->boxes.size();++i){assert(a->boxes[i].text==b->boxes[i].text);assert(a->boxes[i].content.x==b->boxes[i].content.x&&a->boxes[i].content.y==b->boxes[i].content.y&&a->boxes[i].content.width==b->boxes[i].content.width&&a->boxes[i].content.height==b->boxes[i].content.height);}}
static unsigned scalar_len(unsigned char c){return c<128?1:2;}// Fixture repertoire deliberately ASCII/Latin1 only.
static void check_literal(const q2d_cases::Case& c,HF f){
 assert(f->normalization);const auto& p=*f->normalization;assert(p.profile==tt::HtmlWhiteSpaceProfile::nowrap&&p.binding==f->run->binding);assert(p.decoded_input==c.decoded);assert(p.normalized_output==c.normalized);assert(f->run->source_text==c.normalized);assert(p.map.size()==c.ranges.size());
 unsigned pos=0;for(unsigned i=0;i<c.ranges.size();++i){const auto len=scalar_len(c.decoded.at(pos));const auto& m=p.map[i];assert(m.piece_ordinal==0&&m.local_input_begin==pos&&m.global_decoded_begin==pos&&m.input_length==len);assert(m.output_begin==c.ranges[i].first&&m.output_length==c.ranges[i].second);pos+=len;}assert(pos==c.decoded.size());
 assert(f->source_map.size()==(c.decoded.empty()?0u:1u));if(!f->source_map.empty()){assert(f->source_map[0].ordinal==0&&f->source_map[0].begin==0&&f->source_map[0].end==c.decoded.size());}
 assert(f->work.image_commands==0);bool line=false;for(auto& box:f->boxes)if(box.text){line=true;assert(box.content.height==64);if(c.normalized.empty())assert(box.content.width==0);}assert(line);if(c.normalized.empty()){assert(f->run->glyphs.empty());assert(f->work.native_glyph_commands==0);}
}
int main(){
 static_assert(!std::is_default_constructible_v<tt::HtmlNormalizationProof>);
 static_assert(!std::is_copy_constructible_v<tt::HtmlNormalizationProof>);
 static_assert(!std::is_move_constructible_v<tt::HtmlNormalizationProof>);
 auto b=bind();auto cases=q2d_cases::corpus();assert(cases.size()>=40);for(auto& c:cases){std::cerr<<"fixture "<<c.name<<"\n";check_literal(c,nowrap_ok(c.raw,b));++fixtures;}
 // Exact comment provenance: A[space] <!--zero--> TAB B.
 auto comment=nowrap_ok("A <!--zero-->\tB",b);const auto& cm=comment->normalization->map;
 assert(comment->run->source_text=="A B"&&comment->normalization->decoded_input=="A \tB");assert(cm.size()==5);
 const unsigned expected[5][6]={{0,0,1,0,0,1},{0,1,1,1,1,1},{1,0,0,2,1,0},{2,0,1,2,1,1},{2,1,1,3,2,1}};
 for(unsigned i=0;i<5;++i){auto& m=cm[i];auto& e=expected[i];assert(m.piece_ordinal==e[0]&&m.local_input_begin==e[1]&&m.input_length==e[2]&&m.global_decoded_begin==e[3]&&m.output_begin==e[4]&&m.output_length==e[5]);}
 assert(comment->source_map.size()==3&&comment->source_map[0].end==2&&comment->source_map[1].begin==2&&comment->source_map[1].end==2&&comment->source_map[2].begin==2&&comment->source_map[2].end==4);++fixtures;
 for(const auto& raw:{"A<!--c--> B","A <!--c-->B"}){
  auto f=nowrap_ok(raw,b);assert(f->run->source_text=="A B");const auto& map=f->normalization->map;assert(map.size()==4);
  const bool before=std::string(raw)=="A<!--c--> B";
  const unsigned a[4][6]={{0,0,1,0,0,1},{1,0,0,1,1,0},{2,0,1,1,1,1},{2,1,1,2,2,1}};
  const unsigned z[4][6]={{0,0,1,0,0,1},{0,1,1,1,1,1},{1,0,0,2,1,0},{2,0,1,2,2,1}};
  for(unsigned i=0;i<4;++i){auto& e=before?a[i]:z[i];auto& m=map[i];assert(m.piece_ordinal==e[0]&&m.local_input_begin==e[1]&&m.input_length==e[2]&&m.global_decoded_begin==e[3]&&m.output_begin==e[4]&&m.output_length==e[5]);}++fixtures;
 }
 // Zero-length comments also retain anchors before/after content and when
 // all whitespace disappears; these are explicit provenance expectations.
 for(const auto& raw:std::vector<std::string>{"<!--zero-->"," <!--zero-->\t", "<!--zero-->A", "A<!--zero-->"}){
  auto f=nowrap_ok(raw,b);const auto& m=f->normalization->map;unsigned comments=0;
  for(const auto& e:m)if(e.input_length==0){++comments;assert(e.output_length==0);assert(e.output_begin==(raw=="A<!--zero-->"?1u:0u));}
  assert(comments==1);++fixtures;
 }
 // Split every valid scalar boundary, including before/after collapsed runs.
 for(const auto& text:std::vector<std::string>{" A  B ","\tA\n B\t","\xc3\xa9  \xc3\xa9","A \xc2\xa0 B"," \t\n","AB"}){
  auto base=nowrap_ok(text,b);for(unsigned pos=0;pos<=text.size();++pos){if(pos<text.size()&&(static_cast<unsigned char>(text[pos])&0xc0)==0x80)continue;auto split=text.substr(0,pos)+"<!--cut-->"+text.substr(pos);auto f=nowrap_ok(split,b);assert(f->run->source_text==base->run->source_text);geometry_equal(base,f);assert(f->normalization->decoded_input==text);assert(f->normalization->map.size()==base->normalization->map.size()+1);++fixtures;}
 }
 auto base=nowrap_ok("A B",b);for(const auto& t:{"  A   B   ","\n\tA\t \nB\n","A<!--x--> \t<!--y--> B","<!--lead-->A B<!--tail-->"}){geometry_equal(base,nowrap_ok(t,b));++fixtures;}
 // Explicit control policy, both raw bytes and decimal entities.
 for(unsigned control=0;control<32;++control){if(control==9||control==10)continue;bad_nowrap(std::string("A")+char(control)+"B",b);bad_nowrap("A&#"+std::to_string(control)+";B",b);}
 for(const auto& t:{"&#xD;","&#xC;","&#xB;","\x7f","&#127;","&#128;","&#173;","&#256;","&#x1f600;"})bad_nowrap(t,b);
 // Input cap is before normalization, not after; all-collapse explicit line.
 for(unsigned n:{4095u,4096u}){auto f=nowrap_ok(std::string(n,' '),b);assert(f->normalization->decoded_input.size()==n&&f->normalization->map.size()==n&&f->run->source_text.empty());++fixtures;}
 std::string many_comments(4096,' ');for(unsigned i=0;i<120;++i)many_comments+="<!--c-->";
 auto bounded=nowrap_ok(many_comments,b);assert(bounded->normalization->map.size()==4216&&bounded->source_map.size()==121);assert(bounded->normalization->map.size()<=4224);++fixtures;
 for(unsigned i=0;i<8;++i)many_comments+="<!--c-->";bad_nowrap(many_comments,b);
 bad_nowrap(std::string(4097,' '),b);bad_nowrap(std::string(257,'A'),b);
 // pre keeps spaces, identity ranges and identical native image to nowrap when
 // no normalization is needed; tabs/newlines remain forbidden in pre.
 for(const auto& t:std::vector<std::string>{"AB"," A  B ","\xc3\xa9 \xc2\xa0"}){auto pre=ok(q2c_cases::document(q2c_cases::owner(t)),b);assert(pre->run->source_text==t);assert(pre->normalization->profile==tt::HtmlWhiteSpaceProfile::pre&&pre->normalization->binding==b);unsigned pos=0;for(auto& m:pre->normalization->map){unsigned len=scalar_len(t.at(pos));assert(m.global_decoded_begin==pos&&m.local_input_begin==pos&&m.input_length==len&&m.output_begin==pos&&m.output_length==len);pos+=len;}assert(pos==t.size());if(t=="AB")geometry_equal(pre,nowrap_ok(t,b));++fixtures;}
 for(const auto& t:{"A\tB","A\nB","A&#9;B","A&#10;B"}){auto r=tt::render_real_html_text(q2c_cases::document(q2c_cases::owner(t)),b,{3072,4096},256,128);assert(std::holds_alternative<tt::HtmlTextError>(r));++fixtures;}
 // Exhaustive observed cancellation and scalar-new allocation failpoints on
 // mixed collapse/comment/multibyte path. Prior returned frame stays unchanged.
 const auto html=nowrap_html(" \tA <!--zero-->\n&#233; B\n");auto stable=ok(html,b);const auto pixels=stable->frame.pixels;const auto prior=stable->normalization;
 Cancel probe(~0u);assert(std::holds_alternative<HF>(tt::render_real_html_text(html,b,{3072,4096},256,128,&probe)));unsigned cancels=0;assert(probe.calls<30000);
 for(unsigned i=1;i<=probe.calls;++i){Cancel c(i);auto r=tt::render_real_html_text(html,b,{3072,4096},256,128,&c);assert(std::holds_alternative<tt::HtmlTextError>(r));assert(std::get<tt::HtmlTextError>(r)==tt::HtmlTextError::cancelled);++cancels;assert(stable->frame.pixels==pixels&&stable->normalization==prior);}
 allocation_count=0;auto op=tt::render_real_html_text(html,b,{3072,4096},256,128);auto count=allocation_count;assert(std::holds_alternative<HF>(op));unsigned ooms=0;
 for(std::size_t i=0;i<count;++i){fail_after=i;auto r=tt::render_real_html_text(html,b,{3072,4096},256,128);fail_after=-1;if(std::holds_alternative<tt::HtmlTextError>(r)){if(std::get<tt::HtmlTextError>(r)!=tt::HtmlTextError::out_of_memory){std::cerr<<"bad OOM site "<<i<<" error "<<int(std::get<tt::HtmlTextError>(r))<<"\n";return 1;}++ooms;}assert(stable->frame.pixels==pixels&&stable->normalization==prior);}
 assert(ooms>0&&cancels>0);std::cout<<"Q2d fixtures="<<fixtures<<" literal_cases="<<cases.size()<<" cancellations="<<cancels<<" allocation_sites="<<count<<" typed_ooms="<<ooms<<" lastgood=intact\n";
}
