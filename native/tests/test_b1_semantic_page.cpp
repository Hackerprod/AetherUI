// Contract checks and ABI calls must remain active in Release builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
// Public ABI contract: DOM recovery, selector identity and rejection are distinct
// from the frozen semantic-to-div pixel equivalence consumer.
#include <aether/aether_b0_source.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
static std::vector<unsigned char> read(const char* p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
static aether_b0_source_options_v1 options;
static aether_b0_source_diagnostic_v1 diagnostic(){aether_b0_source_diagnostic_v1 d{};d.struct_size=sizeof d;d.version=1;return d;}
static aether_b0_source_frame_info_v1 info(){aether_b0_source_frame_info_v1 i{};i.struct_size=sizeof i;i.version=1;return i;}
static std::string document(const std::string& head,const std::string& body){return "<html><head>"+head+"</head><body>"+body+"</body></html>";}
static void tag(aether_node_t n,const char* expected){assert(n);char* t=aether_element_get_tag_name(n);assert(t&&!std::strcmp(t,expected));aether_string_free(t);}
static void recovered(const std::string& html,const char* expected_parent){aether_doc_t d{};assert(aether_doc_create(&d)==AETHER_OK);assert(aether_doc_parse_html(d,html.c_str())==AETHER_OK);auto n=aether_doc_get_element_by_id(d,"x");if(expected_parent){tag(n,"section");tag(aether_node_get_parent(n),expected_parent);}else assert(!n);aether_doc_destroy(d);}
static std::vector<unsigned char> accept(const std::string& html){aether_b0_source_t s{};auto d=diagnostic();int rc=aether_b0_source_create(640,400,html.data(),html.size(),&options,&s,&d);if(rc)std::fprintf(stderr,"unexpected reject %d stage %u: %s\n",rc,d.stage,html.c_str());assert(rc==AETHER_OK&&s);auto i=info();assert(aether_b0_source_copy_frame(s,&i,nullptr,0)==AETHER_ERROR_NO_FRAME);assert(aether_b0_source_render(s,&d)==AETHER_OK);assert(aether_b0_source_copy_frame(s,&i,nullptr,0)==AETHER_OK&&i.sequence==1);std::vector<unsigned char> p(i.byte_count);assert(aether_b0_source_copy_frame(s,&i,p.data(),p.size())==AETHER_OK);assert(aether_b0_source_destroy(s)==AETHER_OK);return p;}
static void reject(const std::string& html,unsigned stage=0){aether_b0_source_t s{};auto d=diagnostic();int rc=aether_b0_source_create(640,400,html.data(),html.size(),&options,&s,&d);if(rc!=AETHER_ERROR_UNSUPPORTED||s||(stage&&d.stage!=stage))std::fprintf(stderr,"unexpected outcome %d stage %u: %s\n",rc,d.stage,html.c_str());assert(rc==AETHER_ERROR_UNSUPPORTED&&!s&&d.status==rc);if(stage)assert(d.stage==stage);std::printf("rejected stage=%u reason=%u node=%llu\n",d.stage,d.reason,(unsigned long long)d.node_ordinal);}
static void color(const std::vector<unsigned char>& p,int y,unsigned r,unsigned g,unsigned b){size_t at=(size_t(y)*640+200)*4;assert(p[at]==b&&p[at+1]==g&&p[at+2]==r&&p[at+3]==255);}
int main(int argc,char**argv){assert(argc==3);auto r=read(argv[1]),b=read(argv[2]);options.struct_size=sizeof options;options.version=1;options.regular_font=r.data();options.regular_font_size=r.size();options.bold_font=b.data();options.bold_font_size=b.size();const auto caps=aether_b0_source_capabilities();assert(caps&AETHER_B0_SOURCE_STATIC_STYLESHEET_CONTROLS);assert(caps&AETHER_B0_SOURCE_STATIC_SEMANTIC_PAGE);
 const std::string vp="<meta name='viewport' content='width=device-width, initial-scale=1'>";
 accept(document("<meta charset='utf-8'>"+vp+"<style>aside{background-color:#112233}</style>","<main><section><p>Page</p></section><aside><p>Help</p></aside></main>"));
 for(auto m:{"<meta name='viewport' content='initial-scale=2'>","<meta name='viewport' content='width=640, initial-scale=1'>","<meta name='viewport' content='width=device-width, initial-scale=1, user-scalable=no'>","<meta name='viewport' content='width=device-width, initial-scale=1, viewport-fit=cover'>","<meta name='viewport' content='width=device-width, initial-scale=1' id='extra'>","<meta charset='UTF-8'>","<meta charset='utf-8' name='viewport' content='width=device-width, initial-scale=1'>","<meta name='viewport' content='width=device-width, initial-scale=1, minimum-scale=1'>","<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1'>","<meta name='Viewport' content='width=device-width, initial-scale=1'>"})reject(document(m,"<main></main>"),AETHER_B0_STAGE_DOM);
 reject(document(vp+vp,""),AETHER_B0_STAGE_DOM);reject(document(vp+"<meta name='viewport' content='initial-scale=2'>",""),AETHER_B0_STAGE_DOM);reject(document("",vp),AETHER_B0_STAGE_DOM);
 for(auto leaf:{"p","h1","h2","label","button"}){std::string body="<"+std::string(leaf)+(std::string(leaf)=="button"?" type='button'":"")+"><section id='x'></section></"+leaf+">";auto h=document("",body);recovered(h,leaf);reject(h,AETHER_B0_STAGE_DOM);}
 auto head=document("<section id='x'></section>","");recovered(head,"head");reject(head,AETHER_B0_STAGE_DOM);
 // Raw-text parsing does not create a section child. Title remains inert;
 // style markup is rejected for CSS loss, not a fictitious DOM child.
 auto title=document("<title><section id='x'></section></title>","<p>Body</p>");recovered(title,nullptr);assert(accept(title)==accept(document("","<p>Body</p>")));
 auto style=document("<style><section id='x'></section></style>","");recovered(style,nullptr);reject(style,AETHER_B0_STAGE_CSS);
 // input is void: parser recovers the apparent child as a body sibling.
 auto input=document("","<input type='text'><section id='x'><p>Sibling</p></section></input>");recovered(input,"body");accept(input);
 // Tags retain original identity. Type, class and descendant selectors all
 // participate in normal specificity and source order, without rewriting CSS.
 const auto identity=document("<style>section{background-color:#112233}div{background-color:#445566}main aside{background-color:#778899}.mark{background-color:#abcdef}section{background-color:#aabbcc}</style>","<main><section id='s'><p>S</p></section><div id='d'><p>D</p></div><aside id='a'><p>A</p></aside><section id='c' class='mark'><p>C</p></section></main>");
 aether_doc_t dom{};assert(aether_doc_create(&dom)==AETHER_OK);assert(aether_doc_parse_html(dom,identity.c_str())==AETHER_OK);for(auto id:{"s","d","a","c"})tag(aether_doc_get_element_by_id(dom,id),!std::strcmp(id,"d")?"div":!std::strcmp(id,"a")?"aside":"section");aether_doc_destroy(dom);
 auto px=accept(identity);color(px,0,170,187,204);color(px,24,68,85,102);color(px,48,119,136,153);color(px,72,171,205,239);
 for(auto css:{"main{display:flex}","main{display:none}","@media(min-width:0px){main{color:#ffffff}}","main{border-radius:33px}","main{box-shadow:1px 1px #000000}","main{background:linear-gradient(red,blue)}","main{opacity:0.5}","main{font-family:Arial}",".absent{unknown-property:1}"})reject(document(vp+"<style>"+css+"</style>","<main><p>Page</p></main>"));
 for(auto body:{"<main><input type='email'></main>","<main><input type='checkbox'></main>","<main><button type='submit'>Go</button></main>"})reject(document(vp,body),AETHER_B0_STAGE_DOM);
 reject(document(vp,"<main style='unknown-property:1'><p>Page</p></main>"),AETHER_B0_STAGE_CSS);reject(document(vp+"<style>head{unknown-property:1}</style>","<main></main>"),AETHER_B0_STAGE_CSS);
 // Inline span/br admission is covered by the additive B2 consumer.
 puts("B1 semantic DOM / identity / cascade / negative contract PASS");
}
