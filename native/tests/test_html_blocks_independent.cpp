// Independently authored multi-owner public API integration tests. The author
// also implemented the low-level budget path: its independent review is separate.
#define main q2c_unused_main
#include "test_html_text_real_independent.cpp"
#undef main
#include "../src/rendering/text/real_html_blocks.h"
#include <algorithm>
using BF=std::shared_ptr<const tt::HtmlBlocksFrame>;
static void replace(std::string& s,const std::string& a,const std::string& z){auto p=s.find(a);assert(p!=std::string::npos);s.replace(p,a.size(),z);}
static std::string owner(const std::string& id,const std::string& t,bool nowrap=false,unsigned height=64,const std::string& bg=""){
 auto s=q2c_cases::style();if(nowrap)replace(s,"white-space:pre","white-space:nowrap");replace(s,"line-height:64px","line-height:"+std::to_string(height)+"px");if(!bg.empty())s+=";background-color:"+bg;
 auto h=q2c_cases::owner(t,s);replace(h,"id=\"aether-text\"","id=\""+id+"\"");return h;
}
static BF blocks(const std::string& inner,const std::vector<std::string>& ids,Binding b,tt::LineBox line={3072,4096}){
 auto r=tt::render_real_html_blocks(q2c_cases::document(inner),ids,b,line,256,512);if(auto e=std::get_if<tt::HtmlTextError>(&r)){std::cerr<<"expected blocks success error="<<int(*e)<<" owners="<<ids.size()<<" completed_fixtures="<<fixtures<<" first_id="<<(ids.empty()?"":ids.front())<<" inner_bytes="<<inner.size()<<"\n";std::abort();}return std::get<BF>(r);
}
static void reject(const std::string& inner,const std::vector<std::string>& ids,Binding b,tt::LineBox line={3072,4096}){auto r=tt::render_real_html_blocks(q2c_cases::document(inner),ids,b,line,256,512);assert(std::holds_alternative<tt::HtmlTextError>(r));++fixtures;}
static void counters(BF f){std::uint64_t glyphs=0,masks=0,canvas=0,visits=0,raster=0;for(auto& o:f->owners){glyphs+=o.run->glyphs.size();masks+=o.work.run.mask_reserved;canvas+=o.work.run.canvas_reserved;visits+=o.work.run.composite_reserved;raster+=o.work.run.raster_reserved;assert(o.run->binding==o.normalization->binding&&o.run->source_text==o.normalization->normalized_output);}
 assert(f->work.cmap_reserved==glyphs*17&&f->work.outline_reserved==glyphs*1000000);assert(f->work.mask_reserved==masks&&f->work.canvas_pixels_reserved==canvas&&f->work.composite_visits_reserved==visits&&f->work.raster_reserved==raster);assert(f->work.totals.image_commands==0&&f->work.normalized_scalars==glyphs);assert(f->work.proof_logical_bytes<=2*1024*1024&&f->work.admission_normalization_steps<=1000000);
}
int main(){
 auto b=bind();
 for(unsigned n:{1u,2u,4u,8u}){std::string inner;std::vector<std::string> ids;for(unsigned i=0;i<n;++i){auto id="owner"+std::to_string(i);ids.push_back(id);inner+=owner(id,i%2?" \tB\n":"A",i%2);}std::reverse(ids.begin(),ids.end());auto f=blocks(inner,ids,b);assert(f->owners.size()==n&&f->work.owners==n);for(unsigned i=0;i<n;++i){auto& o=f->owners[i];assert(o.id=="owner"+std::to_string(i)&&o.run->source_text==(i%2?"B":"A"));assert(o.has_command&&o.text_box.content.x==0&&o.text_box.content.y==64*i);if(i)assert(o.command_index>f->owners[i-1].command_index);}counters(f);++fixtures;}
 std::string nine;std::vector<std::string> ids9;for(unsigned i=0;i<9;++i){auto id="n"+std::to_string(i);ids9.push_back(id);nine+=owner(id,"A");}reject(nine,ids9,b);
 reject(owner("x","A"),{},b);reject(owner("x","A"),{"missing"},b);reject(owner("x","A"),{"x","x"},b);reject(owner("x","A")+owner("x","B"),{"x"},b);reject(owner("x","A")+owner("other","B"),{"x"},b);
 // Reject unsupported text/styles in a later owner without partial output.
 for(const auto& bad:std::vector<std::string>{"&#0;","&#xD800;","&#x110000;","&#x100;","&#x1f600;","&#173;","&unknown;","&#9;","&#10;","\r","\f","\v","<br>","<img>","<script>A</script>","<span>A</span>"})reject(owner("a","A")+owner("b",bad),{"a","b"},b);
 reject(owner("a",owner("b","B")),{"a","b"},b);
 for(const auto& invalid:std::vector<std::string>{"",std::string(65,'x')})reject(owner("a","A"),{invalid},b);
 for(auto dimensions:std::vector<std::pair<unsigned,unsigned>>{{0,512},{256,0},{2049,512},{256,2049}}){auto r=tt::render_real_html_blocks(q2c_cases::document(owner("a","A")),{"a"},b,{3072,4096},dimensions.first,dimensions.second);assert(std::holds_alternative<tt::HtmlTextError>(r));++fixtures;}
 {auto r=tt::render_real_html_blocks(q2c_cases::document(owner("a","A")),{"a"},nullptr,{3072,4096},256,512);assert(std::holds_alternative<tt::HtmlTextError>(r));++fixtures;}
 auto equal=blocks(owner("first","AA")+owner("middle","<!--empty-->")+owner("last","AA"),{"last","first","middle"},b);
 assert(equal->owners.size()==3&&equal->owners[1].id=="middle"&&!equal->owners[1].has_command&&equal->owners[1].text_box.content.height==64&&equal->owners[1].text_box.content.width==0&&equal->owners[2].text_box.content.y==128);
 assert(equal->owners[0].run!=equal->owners[2].run&&equal->owners[0].normalization!=equal->owners[2].normalization);assert(equal->owners[0].run->canvas.rgba==equal->owners[2].run->canvas.rgba);assert(equal->work.totals.native_glyph_commands==2);counters(equal);++fixtures;
 // Source pieces are local per owner; entity bytes and empty comment retained.
 auto pieces=blocks(owner("x","A <!--c-->\t&#233;",true)+owner("y"," &nbsp; ",true),{"y","x"},b);
 assert(pieces->owners[0].normalization->decoded_input=="A \t\xc3\xa9"&&pieces->owners[0].run->source_text=="A \xc3\xa9");assert(pieces->owners[0].source_map.size()==3&&pieces->owners[0].source_map[2].end==5);assert(pieces->owners[1].run->source_text=="\xc2\xa0");counters(pieces);++fixtures;
 // Actual public painting-order oracle: glyph1 ink extends below its one-pixel
 // LineBox into owner2. Its opaque blue background must cover that ink.
 auto overlap=blocks(owner("ink","A",false,1)+owner("cover","",false,1,"#0000ff"),{"cover","ink"},b,{0,64});
 auto control=blocks(owner("ink","A",false,1)+owner("cover","",false,1),{"cover","ink"},b,{0,64});
 assert(overlap->owners[1].text_box.content.y==1);auto at=overlap->frame.stride+3*4;assert(overlap->frame.pixels[at]==255&&overlap->frame.pixels[at+1]==0&&overlap->frame.pixels[at+2]==0&&overlap->frame.pixels[at+3]==255);assert(control->frame.pixels[at+2]==0&&control->frame.pixels[at+3]>0);assert(overlap->frame.pixels!=control->frame.pixels);++fixtures;
 auto ink2=blocks(owner("ink","A",false,1)+owner("second","A",false,1),{"second","ink"},b,{0,64});
 assert(ink2->owners[0].command_index<ink2->owners[1].command_index);assert(ink2->frame.pixels[at]==0&&ink2->frame.pixels[at+1]==0&&ink2->frame.pixels[at+2]==0&&ink2->frame.pixels[at+3]==255);assert(ink2->frame.pixels[at+3]>control->frame.pixels[at+3]);++fixtures;
 // Fractional LineBox is admitted for owner1, but a fractional owner2 origin
 // must reject the entire result rather than rounding it.
 auto frac=owner("f","A");replace(frac,"line-height:64px","line-height:64.5px");
 auto onlyfrac=blocks(frac,{"f"},b,{3072,4128});assert(onlyfrac->owners[0].text_box.content.height==64.5f);++fixtures;
 auto frac2=owner("g","B");replace(frac2,"line-height:64px","line-height:64.5px");
 auto rejectfrac=tt::render_real_html_blocks(q2c_cases::document(frac+frac2),{"f","g"},b,{3072,4128},256,512);assert(std::holds_alternative<tt::HtmlTextError>(rejectfrac)&&std::get<tt::HtmlTextError>(rejectfrac)==tt::HtmlTextError::unsupported);++fixtures;
 auto exact_error=[&](const std::string& raw,const std::vector<std::string>& ids,tt::HtmlTextError e){auto r=tt::render_real_html_blocks(q2c_cases::document(raw),ids,b,{3072,4096},256,512);assert(std::holds_alternative<tt::HtmlTextError>(r)&&std::get<tt::HtmlTextError>(r)==e);++fixtures;};
 exact_error(owner("x","A"),{},tt::HtmlTextError::invalid_input);
 exact_error(nine,ids9,tt::HtmlTextError::limit_exceeded);
 exact_error(owner("x","A"),{"x","x"},tt::HtmlTextError::invalid_input);
 exact_error(owner("x","A"),{"missing"},tt::HtmlTextError::unsupported);
 // A single-owner document is byte/geometry equivalent to the established API.
 auto onehtml=q2c_cases::document(owner("aether-text"," A  B "));auto single=tt::render_real_html_text(onehtml,b,{3072,4096},256,512);auto multi=tt::render_real_html_blocks(onehtml,{"aether-text"},b,{3072,4096},256,512);assert(std::holds_alternative<HF>(single)&&std::holds_alternative<BF>(multi));assert(std::get<HF>(single)->frame.pixels==std::get<BF>(multi)->frame.pixels);assert(std::get<HF>(single)->run->source_text==std::get<BF>(multi)->owners[0].run->source_text);++fixtures;
 // Shared budgets and no per-owner glyph-count reset. Two owners total256 allowed.
 const auto boundHtml=q2c_cases::document(owner("a",std::string(128,'A'))+owner("b",std::string(128,'A')));
 auto boundResult=tt::render_real_html_blocks(boundHtml,{"b","a"},b,{3072,4096},2048,512);assert(std::holds_alternative<BF>(boundResult));auto bound=std::get<BF>(boundResult);
 assert(bound->work.normalized_scalars==256&&bound->work.cmap_reserved==4352&&bound->work.outline_reserved==256000000);for(const auto& o:bound->owners)assert(o.text_box.content.x>=0&&o.text_box.content.x+o.text_box.content.width<=2048);counters(bound);++fixtures;
 // Known inherited viewport-scroll limitation: same document exceeding viewport
 // width generates implicit root-scroll state unsupported by this closed profile.
 const auto boundedPixels=bound->frame.pixels;auto overflow=tt::render_real_html_blocks(boundHtml,{"b","a"},b,{3072,4096},256,512);assert(std::holds_alternative<tt::HtmlTextError>(overflow)&&std::get<tt::HtmlTextError>(overflow)==tt::HtmlTextError::preparation_failed);assert(bound->frame.pixels==boundedPixels);++fixtures;
 reject(owner("a",std::string(128,'A'))+owner("b",std::string(129,'A')),{"a","b"},b);
 auto collapsed=blocks(owner("a",std::string(2048,' '),true)+owner("b",std::string(2048,' '),true),{"b","a"},b);assert(collapsed->work.totals.decoded_input_bytes==4096&&collapsed->work.normalized_scalars==0&&collapsed->work.totals.native_glyph_commands==0);counters(collapsed);++fixtures;
 reject(owner("a",std::string(2048,' '),true)+owner("b",std::string(2049,' '),true),{"a","b"},b);
 reject(owner("a","A")+owner("b","&#13;",true),{"a","b"},b);reject(owner("a","A")+owner("b","\t"),{"a","b"},b);reject(owner("a","A")+owner("b","<span>B</span>"),{"a","b"},b);
 // Second owner can fail font lookup after a valid first run; no partial
 // collection escapes. Aggregate scalar rejection precedes missing-glyph work.
 auto missing=bind(1024,true);reject(owner("a","A")+owner("b","B"),{"a","b"},missing);
 auto beforelookup=tt::render_real_html_blocks(q2c_cases::document(owner("a",std::string(128,'A'))+owner("b",std::string(129,'B'))),{"a","b"},missing,{3072,4096},256,512);assert(std::holds_alternative<tt::HtmlTextError>(beforelookup)&&std::get<tt::HtmlTextError>(beforelookup)==tt::HtmlTextError::limit_exceeded);++fixtures;
 // Fractional baseline is supported; fractional next-owner layout origin is not.
 auto phase=blocks(owner("a","A")+owner("b","B"),{"a","b"},b,{3073,4096});assert(phase->owners[0].run->glyphs[0].mask->phase_y==1&&phase->owners[1].run->glyphs[0].mask->phase_y==1);++fixtures;
 // Enumerate every observed callback / scalar-new allocation failure on a
 // two-owner mixed-profile document. No successful partial collection may escape.
 const auto html=q2c_cases::document(owner("a","A")+owner("b"," \t&#233;\n",true));const std::vector<std::string> ids{"b","a"};auto stable=std::get<BF>(tt::render_real_html_blocks(html,ids,b,{3072,4096},256,512));auto pixels=stable->frame.pixels;auto proof=stable->owners[1].normalization;
 Cancel probe(~0u);auto pr=tt::render_real_html_blocks(html,ids,b,{3072,4096},256,512,&probe);assert(std::holds_alternative<BF>(pr)&&probe.calls<50000);
 unsigned canceled=0;for(unsigned i=1;i<=probe.calls;++i){Cancel c(i);auto r=tt::render_real_html_blocks(html,ids,b,{3072,4096},256,512,&c);assert(std::holds_alternative<tt::HtmlTextError>(r)&&std::get<tt::HtmlTextError>(r)==tt::HtmlTextError::cancelled);++canceled;assert(stable->frame.pixels==pixels&&stable->owners.size()==2&&stable->owners[1].normalization==proof);}
 allocation_count=0;auto op=tt::render_real_html_blocks(html,ids,b,{3072,4096},256,512);auto count=allocation_count;assert(std::holds_alternative<BF>(op));unsigned ooms=0;
 for(std::size_t i=0;i<count;++i){fail_after=i;auto r=tt::render_real_html_blocks(html,ids,b,{3072,4096},256,512);fail_after=-1;if(std::holds_alternative<tt::HtmlTextError>(r)){if(std::get<tt::HtmlTextError>(r)!=tt::HtmlTextError::out_of_memory){std::cerr<<"OOM site="<<i<<" error="<<int(std::get<tt::HtmlTextError>(r))<<"\n";return 1;}++ooms;}assert(stable->frame.pixels==pixels&&stable->owners[1].normalization==proof);}assert(ooms>0);
 std::cout<<"Q2e public fixtures="<<fixtures<<" canceled="<<canceled<<" allocation_sites="<<count<<" typed_ooms="<<ooms<<" lastgood=intact overlap=verified legacy=exact rebase=separate_internal_gate\n";
}
