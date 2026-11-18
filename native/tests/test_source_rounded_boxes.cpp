// Public-ABI geometry oracle: deliberately does not include renderer internals.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <aether/aether_b0_source.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static aether_b0_source_options_v1 options{};
static constexpr int width=160,height=128;
static std::vector<unsigned char> read(const char* p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
static aether_b0_source_diagnostic_v1 diagnostic(){aether_b0_source_diagnostic_v1 d{};d.struct_size=sizeof d;d.version=1;return d;}
static aether_b0_source_frame_info_v1 info(){aether_b0_source_frame_info_v1 i{};i.struct_size=sizeof i;i.version=1;return i;}
static std::string document(const std::string& css,const std::string& body){return "<html><head><style>body{background-color:#ffffff;}"+css+"</style></head><body>"+body+"</body></html>";}
static std::vector<unsigned char> render(const std::string& html){
 aether_b0_source_t s{};auto d=diagnostic();int rc=aether_b0_source_create(width,height,html.data(),html.size(),&options,&s,&d);
 if(rc)std::fprintf(stderr,"unexpected create %d stage %u reason %u: %s\n",rc,d.stage,d.reason,html.c_str());
 assert(rc==AETHER_OK&&s);auto i=info();assert(aether_b0_source_copy_frame(s,&i,nullptr,0)==AETHER_ERROR_NO_FRAME);
 rc=aether_b0_source_render(s,&d);if(rc)std::fprintf(stderr,"unexpected render %d stage %u reason %u\n",rc,d.stage,d.reason);assert(rc==AETHER_OK);
 assert(aether_b0_source_copy_frame(s,&i,nullptr,0)==AETHER_OK&&i.sequence==1&&i.width==width&&i.height==height);
 assert(i.format==AETHER_PIXEL_FORMAT_BGRA8_PREMULTIPLIED&&i.stride==width*4);
 std::vector<unsigned char> p(i.byte_count),again(i.byte_count);assert(aether_b0_source_copy_frame(s,&i,p.data(),p.size())==AETHER_OK);
 assert(aether_b0_source_render(s,&d)==AETHER_OK);assert(aether_b0_source_copy_frame(s,&i,again.data(),again.size())==AETHER_OK&&i.sequence==1&&again==p);
 assert(aether_b0_source_destroy(s)==AETHER_OK);return p;
}
static void reject(const std::string& html,unsigned stage,unsigned reason=AETHER_B0_REASON_UNSUPPORTED){
 aether_b0_source_t s{};auto d=diagnostic();int rc=aether_b0_source_create(width,height,html.data(),html.size(),&options,&s,&d);
 if(rc!=AETHER_ERROR_UNSUPPORTED||s||d.stage!=stage||d.reason!=reason)std::fprintf(stderr,"unexpected reject %d stage %u reason %u: %s\n",rc,d.stage,d.reason,html.c_str());
 assert(rc==AETHER_ERROR_UNSUPPORTED&&!s&&d.status==rc&&d.stage==stage&&d.reason==reason);
}
// Signed Euclidean distance to a mathematically defined circular rounded box.
// This uses no paint command, tessellation, sample grid or kernel implementation.
static double distance(double x,double y,double left,double top,double w,double h,double r){
 const double qx=std::abs(x-left-w/2)-(w/2-r),qy=std::abs(y-top-h/2)-(h/2-r);
 return std::hypot(std::fmax(qx,0),std::fmax(qy,0))+std::fmin(std::fmax(qx,qy),0)-r;
}
static void color(const std::vector<unsigned char>& p,int x,int y,int r,int g,int b){const auto at=(y*width+x)*4;
 if(p[at]!=b||p[at+1]!=g||p[at+2]!=r||p[at+3]!=255)std::fprintf(stderr,"pixel %d,%d got %u,%u,%u,%u expected %d,%d,%d,255\n",x,y,p[at+2],p[at+1],p[at],p[at+3],r,g,b);
 assert(p[at]==b&&p[at+1]==g&&p[at+2]==r&&p[at+3]==255);
}
static std::string box_css(double w,double h,int radius,double border,bool transparent=false,int left=16,int top=16){
 return "body{padding-top:"+std::to_string(top)+"px;}div{width:"+std::to_string(w)+"px;padding-top:"+std::to_string(h)+"px;margin-left:"+std::to_string(left)+"px;background-color:"+(transparent?"transparent":"#2244aa")+";border-radius:"+std::to_string(radius)+"px;border-width:"+std::to_string(border)+"px;border-style:solid;border-color:#cc2200;}";
}
static void geometry(double w,double h,int declared,double b,bool transparent=false,int left=16,int top=16){
 const auto pixels=render(document(box_css(w,h,declared,b,transparent,left,top),"<div></div>"));
 const double ow=w+2*b,oh=h+2*b,r=std::fmin(double(declared),std::fmin(ow,oh)/2),ir=std::fmax(r-b,0);
 unsigned checked=0;
 for(int y=0;y<height;++y)for(int x=0;x<width;++x){const auto outer=distance(x+.5,y+.5,left,top,ow,oh,r);
  const auto inner=distance(x+.5,y+.5,left+b,top+b,w,h,ir);
  // Ignore a full pixel diagonal around edges, independently of AA convention.
  if(std::abs(outer)<1.5||(b>0&&std::abs(inner)<1.5))continue;
  if(outer>0)color(pixels,x,y,255,255,255);
  else if(b>0&&inner>0)color(pixels,x,y,204,34,0);
  else if(transparent)color(pixels,x,y,255,255,255);
  else color(pixels,x,y,34,68,170);
  ++checked;
 }
 assert(checked>1000);
}
int main(int argc,char**argv){
 assert(argc==3);auto regular=read(argv[1]),bold=read(argv[2]);options.struct_size=sizeof options;options.version=1;
 options.regular_font=regular.data();options.regular_font_size=regular.size();options.bold_font=bold.data();options.bold_font_size=bold.size();
 assert((aether_b0_source_capabilities()&AETHER_B0_SOURCE_STATIC_ROUNDED_BOXES)!=0);
 for(int radius:{0,1,12,32})geometry(80,60,radius,0);
 for(int b:{1,2,8})geometry(80,60,12,b);
 geometry(80,60,8,8); // b == R: square interior, circular exterior.
 geometry(23.5,31.5,32,2); // used R=13.75, fractional normalization.
 geometry(4,5,32,1);geometry(80,60,12,0,true);geometry(80,60,12,2,true);
 geometry(80,60,32,2,false,130,100); // viewport clipping does not alter shape.
 // Transparent canvas: independently verify premultiplication, including AA
 // corner samples, rather than only opaque composition over white.
 auto alpha=render(document("body{background-color:transparent;}"+box_css(80,60,12,0),"<div></div>"));
 unsigned partial=0;
 for(size_t at=0;at<alpha.size();at+=4){const int a=alpha[at+3];if(a>0&&a<255)++partial;
  for(int channel=0;channel<3;++channel){const int rgb=channel==0?170:channel==1?68:34;
   assert(std::abs(int(alpha[at+channel])-a*rgb/255.0)<=1.0);assert(alpha[at+channel]<=a);
  }
 }
 assert(partial>0);
 auto transparent=render(document("body{background-color:transparent;}"+box_css(80,60,12,0,true),"<div></div>"));
 for(auto byte:transparent)assert(byte==0);
 // Lexically decimal/exponent forms with integral numeric values stay valid.
 for(const auto* value:{"0","1.0px","1e1px","32px"})render(document("div{padding-top:40px;border-radius:"+std::string(value)+";}","<div></div>"));
 for(const auto* value:{"-1px","33px","1.5px","50%","1em","calc(1px)","var(--r)","1px 2px","1px 2px 3px 4px","1px / 1px"})
  reject(document("div{border-radius:"+std::string(value)+";}","<div></div>"),AETHER_B0_STAGE_CSS,AETHER_B0_REASON_CSS_LOSS);
 reject(document("div{border-top-left-radius:1px;}","<div></div>"),AETHER_B0_STAGE_CSS,AETHER_B0_REASON_CSS_LOSS);
 reject(document(".absent{border-radius:33px;}","<div></div>"),AETHER_B0_STAGE_CSS,AETHER_B0_REASON_CSS_LOSS);
 render(document(".absent{border-radius:12px;}","<div></div>"));
 for(const auto* tag:{"html","body","head","style","title","meta","span","strong","a","br"}){
  const std::string body=std::string(tag)=="span"||std::string(tag)=="strong"||std::string(tag)=="a"?"<p><"+std::string(tag)+">Text</"+tag+"></p>":std::string(tag)=="br"?"<p>A<br>B</p>":"<p>Text</p>";
  std::string h=document(std::string(tag)+"{border-radius:1px;}",body);
  if(std::string(tag)=="title")h.insert(h.find("<style>"),"<title>Title</title>");
  if(std::string(tag)=="meta")h.insert(h.find("<style>"),"<meta charset='utf-8'>");
  reject(h,AETHER_B0_STAGE_STYLE);
 }
 // Positive radii on every allowed nonfragmented tag, including empty controls.
 for(const auto* tag:{"div","main","section","aside","form","h1","h2","p","label","button","input"}){
  const std::string extra=std::string(tag)=="button"?" type='button'":std::string(tag)=="input"?" type='text'":"";
  render(document(std::string(tag)+"{padding-top:12px;padding-bottom:12px;border-radius:12px;}","<"+std::string(tag)+extra+">"+(std::string(tag)=="input"?"":"</"+std::string(tag)+">")));
 }
 reject(document(box_css(80,60,12,9),"<div></div>"),AETHER_B0_STAGE_LAYOUT);
 reject(document(box_css(80,60,1,2),"<div></div>"),AETHER_B0_STAGE_LAYOUT);
 // Positive radii on empty/zero-width used boxes must fail explicitly.
 reject(document(box_css(0,0,12,0),"<div></div>"),AETHER_B0_STAGE_LAYOUT);
 reject(document("div{border-radius:12px;border-left-width:2px;}","<div></div>"),AETHER_B0_STAGE_CSS,AETHER_B0_REASON_CSS_LOSS);
 // Cascade returning to zero preserves the exact historical square bytes.
 const auto square=document(box_css(80,60,0,2),"<div></div>");
 assert(render(square)==render(document("div{border-radius:12px;}"+box_css(80,60,0,2),"<div></div>")));
 // overflow:visible is unchanged. The child's red square covers its parent's
 // rounded corner and extends beyond the parent's right edge.
 const auto overflow=[](int r){return document("section{width:16px;background-color:#2244aa;border-radius:"+std::to_string(r)+"px;}div{width:40px;padding-top:40px;background-color:#cc2200;}","<section><div></div></section>");};
 const auto over=render(overflow(8));assert(over==render(overflow(0)));color(over,0,0,204,34,0);color(over,35,2,204,34,0);
 // A radius changes only corners, not text, content geometry or rectangular
 // input clipping. A suffix wholly outside the content clip remains invisible.
 const auto input=[](const std::string& attributes,int radius){return document("input{width:40px;padding-top:8px;padding-bottom:8px;background-color:#2244aa;color:#ffffff;border-radius:"+std::to_string(radius)+"px;}","<input "+attributes+">");};
 assert(render(input("type='text' value='MMMMMMMMMMMMMMMM'",12))==render(input("type='text' value='MMMMMMMMMMMMMMMMsuffix'",12)));
 assert(render(input("type='text' placeholder='MMMMMMMMMMMMMMMM'",12))==render(input("type='text' placeholder='MMMMMMMMMMMMMMMMsuffix'",12)));
 assert(render(input("type='password' value='secret'",12))==render(input("type='password' value='abcdef'",12)));
 auto round=render(input("type='text' value='AB'",12)),flat=render(input("type='text' value='AB'",0));
 for(int y=12;y<28;++y)for(int x=0;x<40;++x)for(int c=0;c<4;++c)assert(round[(y*width+x)*4+c]==flat[(y*width+x)*4+c]);
 // A dense stack of valid rounded boxes exceeds the unchanged software work
 // budget. No failed render publishes frame metadata or destination bytes.
 std::string nested;for(int n=0;n<10;++n)nested+="<div>";nested+="<div class='leaf'></div>";for(int n=0;n<10;++n)nested+="</div>";
 const auto dense=document("div{width:2048px;border-radius:32px;background-color:#2244aa;}.leaf{padding-top:2048px;}",nested);
 aether_b0_source_t dense_session{};auto dense_d=diagnostic();
 int dense_rc=aether_b0_source_create(2048,2048,dense.data(),dense.size(),&options,&dense_session,&dense_d);
 if(dense_rc)std::fprintf(stderr,"dense create %d stage %u reason %u\n",dense_rc,dense_d.stage,dense_d.reason);
 assert(dense_rc==AETHER_OK&&dense_session);
 for(int n=0;n<2;++n){assert(aether_b0_source_render(dense_session,&dense_d)==AETHER_ERROR_LIMIT_EXCEEDED);
  assert(dense_d.stage==AETHER_B0_STAGE_RASTER&&dense_d.reason==AETHER_B0_REASON_LIMIT);
  auto no_frame=info(),before=no_frame;std::vector<unsigned char> sentinel(width*height*4,0xa5);
  assert(aether_b0_source_copy_frame(dense_session,&no_frame,sentinel.data(),sentinel.size())==AETHER_ERROR_NO_FRAME);
  assert(no_frame.sequence==before.sequence&&no_frame.byte_count==before.byte_count);
  for(auto byte:sentinel)assert(byte==0xa5);
 }
 assert(aether_b0_source_destroy(dense_session)==AETHER_OK);
 puts("SOURCE ROUNDED BOXES: public ABI, geometry, normalization, clipping, controls, diagnostics PASS");
}
