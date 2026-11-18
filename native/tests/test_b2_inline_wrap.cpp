// Production consumer: only installed public headers and the shared library.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <aether/aether_b0_source.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
static std::vector<uint8_t> regular,bold;
static auto diagnostic(){aether_b0_source_diagnostic_v1 d{};d.struct_size=sizeof d;d.version=1;return d;}
static auto information(){aether_b0_source_frame_info_v1 i{};i.struct_size=sizeof i;i.version=1;return i;}
static std::vector<uint8_t> read(const char* p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
static std::string doc(const std::string& body,const std::string& css="",int width=240){return "<html><head><style>body{width:"+std::to_string(width)+"px;background-color:#ffffff}p,div{white-space:normal}"+css+"</style></head><body>"+body+"</body></html>";}
struct Frame{int width=320,height=240;std::vector<uint8_t> pixels;};
static Frame render(std::string html,bool lifecycle=false){
 aether_b0_source_t s{};auto d=diagnostic();
 {auto r=regular,b=bold;aether_b0_source_options_v1 o{};o.struct_size=sizeof o;o.version=1;o.regular_font=r.data();o.regular_font_size=r.size();o.bold_font=b.data();o.bold_font_size=b.size();
  auto rc=aether_b0_source_create(320,240,html.data(),html.size(),&o,&s,&d);if(rc)std::fprintf(stderr,"B2 create=%d stage=%u reason=%u\n",rc,d.stage,d.reason);assert(rc==AETHER_OK&&s);}
 // All actual input buffers supplied to create have been released here.
 html.clear();html.shrink_to_fit();auto i=information(),before=i;
 assert(aether_b0_source_copy_frame(s,&i,nullptr,0)==AETHER_ERROR_NO_FRAME);assert(!std::memcmp(&i,&before,sizeof i));
 if(lifecycle){std::thread t([s]{auto d=diagnostic(),old=d;auto i=information(),before=i;assert(aether_b0_source_render(s,&d)==AETHER_ERROR_INVALID_STATE);assert(!std::memcmp(&d,&old,sizeof d));assert(aether_b0_source_copy_frame(s,&i,nullptr,0)==AETHER_ERROR_INVALID_STATE);assert(!std::memcmp(&i,&before,sizeof i));assert(aether_b0_source_destroy(s)==AETHER_ERROR_INVALID_STATE);});t.join();}
 assert(aether_b0_source_render(s,&d)==AETHER_OK);assert(aether_b0_source_copy_frame(s,&i,nullptr,0)==AETHER_OK);assert(i.width==320&&i.height==240&&i.stride==1280&&i.byte_count==307200&&i.sequence==1&&i.format==AETHER_PIXEL_FORMAT_BGRA8_PREMULTIPLIED);
 std::vector<uint8_t> p(i.byte_count+32,0xa5),q(i.byte_count+32,0x6b);
 assert(aether_b0_source_copy_frame(s,&i,p.data(),1)==AETHER_ERROR_BUFFER_TOO_SMALL);for(auto b:p)assert(b==0xa5);
 assert(aether_b0_source_copy_frame(s,&i,p.data(),p.size())==AETHER_OK);assert(aether_b0_source_render(s,&d)==AETHER_OK);assert(aether_b0_source_copy_frame(s,&i,q.data(),q.size())==AETHER_OK&&i.sequence==1);
 assert(!std::memcmp(p.data(),q.data(),i.byte_count));for(size_t k=i.byte_count;k<p.size();++k)assert(p[k]==0xa5&&q[k]==0x6b);for(size_t k=3;k<i.byte_count;k+=4)assert(p[k]==255);
 p.resize(i.byte_count);assert(aether_b0_source_destroy(s)==AETHER_OK);return {320,240,std::move(p)};
}
static int marker_y(const Frame& f){for(int y=0;y<f.height;++y)for(int x=0;x<f.width;++x){size_t at=(size_t(y)*f.width+x)*4;if(f.pixels[at]==0x45&&f.pixels[at+1]==0x23&&f.pixels[at+2]==0xe1)return y;}assert(false);return -1;}
static const char* marker="<p style='background-color:#e12345'>x</p>";
static void height(const std::string& text,int expected,const char* mode="normal",int width=240){auto f=render(doc("<p style='white-space:"+std::string(mode)+"'>"+text+"</p>"+marker,"",width));auto actual=marker_y(f);if(actual!=expected)std::fprintf(stderr,"height expected=%d actual=%d mode=%s\n",expected,actual,mode);assert(actual==expected);}
static int first_dark_x(const Frame& f){for(int x=0;x<240;++x)for(int y=0;y<24;++y){size_t k=(size_t(y)*f.width+x)*4;if(f.pixels[k]<100&&f.pixels[k+1]<100&&f.pixels[k+2]<100)return x;}assert(false);return -1;}
int main(int argc,char**argv){assert(argc==3);regular=read(argv[1]);bold=read(argv[2]);auto caps=aether_b0_source_capabilities();assert(caps&AETHER_B0_SOURCE_STATIC_STYLESHEET_CONTROLS);assert(caps&AETHER_B0_SOURCE_STATIC_SEMANTIC_PAGE);assert(caps&AETHER_B0_SOURCE_STATIC_INLINE_WRAP);
 render(doc("<p>¿Qué <strong>tal</strong>? <a href='#crear-cuenta'>Cuenta</a><br>Otra línea.</p>"),true);
 for(const char* mode:{"normal","nowrap"}){height("",0,mode);height(" &#9;&#10;&#13;&#12; ",0,mode);height("<br>",24,mode);height("<br><br>",48,mode);height("x<br>",24,mode);height("<br>x",48,mode);height("x<br><br>",48,mode);height("x<br>y",48,mode);}
 // Independent geometry oracle: fixed 24px line height and marker top, not
 // merely equality between split and unsplit paths. A single x fits 12px.
 height("x",24,"normal",12);height("x x",48,"normal",12);height("x x",24,"nowrap",12);
 // Independent hmtx oracle for the pinned DejaVuSans fixture: unitsPerEm=2048,
 // advance(x)=1212, advance(SP)=651, font size=16, hence W=24.0234375.
 // Binary-exact W and W-1/1024 distinguish equality from just-below threshold.
 assert(marker_y(render(doc("<p>x x</p>"+std::string(marker),"body{width:24.0234375px}")))==24);
 assert(marker_y(render(doc("<p>x x</p>"+std::string(marker),"body{width:24.0224609375px}")))==48);
 render(doc("<p><span style='white-space:normal'>same mode</span></p>"));
 height("¿Qu<span>é&nbsp;tal?</span>",24,"normal",1); // overwide indivisible NBSP word
 // hmtx widths [1087,1612,1298,1260,651,803,1255,569,1087],
 // sum=9622 design units: W=75.171875px at 16px/2048. NBSP makes
 // the markup-split word indivisible both at W and W-1/1024.
 auto nb_exact=render(doc("<p>¿Qu<span>é&nbsp;tal?</span></p>"+std::string(marker),"body{width:75.171875px}"));
 auto nb_short=render(doc("<p>¿Qu<span>é&nbsp;tal?</span></p>"+std::string(marker),"body{width:75.1708984375px}"));
 assert(marker_y(nb_exact)==24&&marker_y(nb_short)==24);
 auto nb_over=render(doc("<p>¿Qu<span>é&nbsp;tal?</span></p>","body{width:1px}"));
 assert(!std::memcmp(nb_exact.pixels.data(),nb_over.pixels.data(),320*24*4));
 bool ink_beyond_box=false;for(int y=0;y<24;++y)for(int x=2;x<100;++x){size_t k=(size_t(y)*320+x)*4;if(nb_over.pixels[k]<100&&nb_over.pixels[k+1]<100&&nb_over.pixels[k+2]<100)ink_beyond_box=true;}assert(ink_beyond_box);

 height("pala<span>bra</span>",24,"normal",1); // markup is not a wrap opportunity
 auto un=render(doc("<p>¿Qué tal? Una palabra completa y café.</p>"));auto split=render(doc("<p>¿Qu<span>é</span> tal? Una pala<!--inert-->bra <span>completa</span> y café.</p>"));assert(un.pixels==split.pixels);
 auto ws=render(doc("<p> A\t<span>\n B</span><!--inert-->\r C\f D </p>"));auto collapsed=render(doc("<p>A B C D</p>"));assert(ws.pixels==collapsed.pixels);
 assert(render(doc("<p>A&#9;<span>&#10;B</span><!--x-->&#13;C&#12;D</p>")).pixels==collapsed.pixels);
 auto start=render(doc("<p>x</p>")),center=render(doc("<p style='text-align:center'>x</p>"));assert(first_dark_x(start)<8);assert(first_dark_x(center)>=110&&first_dark_x(center)<=120);
 // Two parent contexts plus the explicit child context = three 24px lines.
 assert(marker_y(render(doc("<div>before<div>inside</div>after</div>"+std::string(marker))))==72);
 // Inter-block whitespace must not add a line or margin-collapse interruption.
 assert(render(doc("<div><p>x</p> \t\n <p>y</p></div>")).pixels==render(doc("<div><p>x</p><p>y</p></div>")).pixels);
 render(doc("<p><span></span><strong></strong><a></a></p><div></div><input type='text'><button type='button'></button>"));
 for(const char* a:{"<a>Cuenta</a>","<a href='#crear-cuenta'>Cuenta</a>","<a href='&#35;crear-cuenta'>Cuenta</a>"})render(doc("<p>"+std::string(a)+"</p>"));
 auto metadata_plain=doc("<p>x</p>");auto metadata_vt=metadata_plain;
 metadata_vt.insert(metadata_vt.find("</head>"),std::string("<title>x")+char(11)+"y</title>");
 assert(render(metadata_plain).pixels==render(metadata_vt).pixels);
 puts("B2 public production lifecycle / pixels / independent line-height oracles PASS");
}
