// Independent public negatives. No case hides another behind one first error.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <aether/aether_b0_source.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
static std::vector<uint8_t> read(const char* p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
static aether_b0_source_options_v1 options;
static std::string doc(const std::string& body,const std::string& css=""){return "<html><head><style>p,div{white-space:normal}"+css+"</style></head><body>"+body+"</body></html>";}
static void reject(const std::string& html,uint32_t stage,int expected=AETHER_ERROR_UNSUPPORTED){aether_b0_source_t s=reinterpret_cast<aether_b0_source_t>(uintptr_t(1));aether_b0_source_diagnostic_v1 d{};d.struct_size=sizeof d;d.version=1;auto rc=aether_b0_source_create(320,240,html.data(),html.size(),&options,&s,&d);if(rc!=expected||s||(stage&&d.stage!=stage))std::fprintf(stderr,"reject expected status=%d stage=%u actual=%d stage=%u reason=%u\n",expected,stage,rc,d.stage,d.reason);assert(rc==expected&&!s&&d.status==rc);if(stage)assert(d.stage==stage);if(expected==AETHER_ERROR_LIMIT_EXCEEDED)assert(d.reason==AETHER_B0_REASON_LIMIT);}
int main(int argc,char**argv){assert(argc==3);auto r=read(argv[1]),b=read(argv[2]);options.struct_size=sizeof options;options.version=1;options.regular_font=r.data();options.regular_font_size=r.size();options.bold_font=b.data();options.bold_font_size=b.size();assert(aether_b0_source_capabilities()&AETHER_B0_SOURCE_STATIC_INLINE_WRAP);
 // Effective mismatch is rejected even if no glyph would be emitted.
 for(const char* body:{"<p><span style='white-space:nowrap'>x</span></p>","<p><span style='white-space:nowrap'></span></p>","<p style='white-space:nowrap'><strong><span style='white-space:normal'>x</span></strong></p>","<p><span style='text-align:center'></span></p>"})reject(doc(body),AETHER_B0_STAGE_STYLE);
 reject(doc("<p>x<br id='b'>y</p>","#b{white-space:nowrap}"),AETHER_B0_STAGE_STYLE);
 reject(doc("<p>x<br id='b'>y</p>","#b{font-size:24px}"),AETHER_B0_STAGE_STYLE);
 // These invalid hierarchies survive this repository's parser; see the
 // separate parsed-DOM observation test, including its void-input repair.
 for(const char* body:{"<p>x<div>y</div></p>","<label><input type='text'></label>","<span><div>x</div></span>","<a><a>x</a></a>","<a><span><a>x</a></span></a>","<p><input type='text'></p>","<p><button type='button'>x</button></p>","<button type='button'><span>x</span></button>","<p><br style='color:#112233'></p>","<p><br lang='es'></p>","<p><br clear='all'></p>","<p><br onclick='x'></p>"})reject(doc(body),AETHER_B0_STAGE_DOM);
 for(const char* css:{"span{display:block}","p{display:inline}","span{padding:1px}","span{width:10px}","span{background-color:#112233}"})reject(doc("<p>x<span>y</span></p>",css),AETHER_B0_STAGE_STYLE);
 for(const char* href:{"#","#1bad","#a b","#a%20","https://example.com","relative","#a?b"," #good","#good "})reject(doc("<p><a href='"+std::string(href)+"'>x</a></p>"),AETHER_B0_STAGE_DOM);
 reject(doc("<p><a href='#"+std::string(65,'a')+"'>x</a></p>"),AETHER_B0_STAGE_DOM);
 reject(doc(std::string("<p>x")+char(11)+"y</p>"),AETHER_B0_STAGE_TEXT);reject(doc("<p>x&#11;y</p>"),AETHER_B0_STAGE_TEXT);
 for(const char* txt:{"x&#173;y","x&#8594;y","x&#10022;y"})reject(doc("<p>"+std::string(txt)+"</p>"),AETHER_B0_STAGE_TEXT);
 reject(doc("<p><span>x</span></p>",".absent{unknown-property:1}"),AETHER_B0_STAGE_CSS);
 reject(doc("<p><span>x</span></p>","head{unknown-property:1}"),AETHER_B0_STAGE_CSS);
 reject(doc("<p><a href='#x'>x</a></p>","a{text-decoration:underline}"),0);
 // Reachable independent limits. The first encountered limit is reported;
 // higher thresholds are tested directly in test_b2_inline_ledger instead.
 std::string comments="<p>";for(int i=0;i<257;++i)comments+="<!--x-->";comments+="</p>";reject(doc(comments),AETHER_B0_STAGE_DOM,AETHER_ERROR_LIMIT_EXCEEDED);
 std::string elements="<p>";for(int i=0;i<253;++i)elements+="<span></span>";elements+="</p>";reject(doc(elements),AETHER_B0_STAGE_DOM,AETHER_ERROR_LIMIT_EXCEEDED);
 reject(doc("<p>"+std::string(8193,' ')+"</p>"),AETHER_B0_STAGE_TEXT,AETHER_ERROR_LIMIT_EXCEEDED);
 puts("B2 public DOM / STYLE / TEXT / CSS-loss / reachable limits PASS");
}
