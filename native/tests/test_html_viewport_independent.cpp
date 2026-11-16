#define main q2c_unused_main
#include "test_html_text_real_independent.cpp"
#undef main
#include "../src/rendering/text/real_html_viewport.h"
#include <array>
#include <cmath>
namespace ip=aether::rendering::preparation;
using IC=aether::rendering::NativeImageCatalog;
using VF=std::shared_ptr<const tt::HtmlViewportFrame>;
static void replace(std::string& s,const std::string& a,const std::string& z){auto p=s.find(a);assert(p!=std::string::npos);s.replace(p,a.size(),z);}
static std::string el(std::string id,std::string text,unsigned w,unsigned h,std::string key=""){
 auto s=q2c_cases::style();replace(s,"width:128px","width:"+std::to_string(w)+"px");replace(s,"line-height:64px","line-height:"+std::to_string(h)+"px");
 if(!key.empty())s+=";background-image:url("+key+");background-repeat:no-repeat;background-position:0px 0px;background-size:auto";
 auto v=q2c_cases::owner(text,s);replace(v,"aether-text",id);return v;
}
static ip::ImageRef im(unsigned w,unsigned h){Bytes pixels(std::size_t(w)*h*4);for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto i=4*(std::size_t(y)*w+x);pixels[i]=x%128;pixels[i+1]=y%128;pixels[i+2]=77;pixels[i+3]=255;}auto r=ip::ImageResource::copy(w,h,pixels);assert(std::holds_alternative<ip::ImageRef>(r));return std::get<ip::ImageRef>(r);}
static VF good(const std::string& raw,const std::vector<std::string>& ids,Binding b,unsigned w,unsigned h,const IC& cat={},tt::ViewportOffset off={},tt::LineBox line={0,4096}){
 auto r=tt::render_real_html_viewport(raw,ids,b,line,w,h,cat,off);if(auto e=std::get_if<tt::HtmlTextError>(&r)){std::cerr<<"Q2g unexpected error="<<int(*e)<<" fixture="<<fixtures<<"\n";std::abort();}return std::get<VF>(r);
}
static void bad(const std::string& raw,const std::vector<std::string>& ids,Binding b,unsigned w,unsigned h,const IC& cat,tt::ViewportOffset off,tt::HtmlTextError e,tt::LineBox line={0,4096}){auto r=tt::render_real_html_viewport(raw,ids,b,line,w,h,cat,off);assert(std::holds_alternative<tt::HtmlTextError>(r)&&std::get<tt::HtmlTextError>(r)==e);++fixtures;}
static void rect(aether_rectf r,float x,float y,float w,float h){assert(r.x==x&&r.y==y&&r.width==w&&r.height==h);}
static unsigned char over(unsigned char src,unsigned alpha,unsigned char dst){return static_cast<unsigned char>((unsigned(src)*alpha+127)/255+(unsigned(dst)*(255-alpha)+127)/255);}
static void close(float a,float b){assert(std::abs(a-b)<0.0001f);}
static void chrome(VF f,unsigned w,unsigned h){auto& d=f->viewport;rect(d.clip,0,0,w,h);auto c=d.chrome;assert(bool(c.horizontal_visible)==bool(d.range_x));assert(bool(c.vertical_visible)==bool(d.range_y));
 // Independent mathematical contract, not production helper invocation.
 if(d.range_x){float L=w-(d.range_y?6:0),len=std::min(L,std::max(std::min(24.f,L),L*L/(L+d.range_x)));rect(c.horizontal_track,0,h-6,L,4);close(c.horizontal_thumb.width,len);close(c.horizontal_thumb.x,(L-len)*d.offset.x/d.range_x);assert(c.horizontal_thumb.y==h-6&&c.horizontal_thumb.height==4);}
 if(d.range_y){float L=h-(d.range_x?6:0),len=std::min(L,std::max(std::min(24.f,L),L*L/(L+d.range_y)));rect(c.vertical_track,w-6,0,4,L);close(c.vertical_thumb.height,len);close(c.vertical_thumb.y,(L-len)*d.offset.y/d.range_y);assert(c.vertical_thumb.x==w-6&&c.vertical_thumb.width==4);}
 if(d.range_x&&d.range_y)rect(c.corner,w-6,h-6,4,4);
}
static std::array<unsigned char,4> pixel(VF f,unsigned x,unsigned y){auto& p=f->scene.blocks.frame;auto n=y*p.stride+x*4;return{p.pixels[n],p.pixels[n+1],p.pixels[n+2],p.pixels[n+3]};}
int main(){
 auto b=bind();auto raw=q2c_cases::document(el("a","A",32,64));auto f=good(raw,{"a"},b,128,128);auto old=tt::render_real_html_blocks_images(raw,{"a"},b,{0,4096},128,128,{});assert(std::holds_alternative<std::shared_ptr<const tt::HtmlBlocksImagesFrame>>(old));assert(f->scene.blocks.frame.pixels==std::get<std::shared_ptr<const tt::HtmlBlocksImagesFrame>>(old)->blocks.frame.pixels);assert(!f->viewport.scrolling&&f->viewport.range_x==0&&f->viewport.range_y==0);++fixtures;
 bad(raw,{"a"},b,128,128,{}, {1,0},tt::HtmlTextError::invalid_input);bad(raw,{"a"},b,128,128,{}, {0,1},tt::HtmlTextError::invalid_input);
 for(auto dims:std::vector<std::pair<unsigned,unsigned>>{{64,32},{32,64},{64,64}}){auto [dw,dh]=dims;auto image=im(dw,dh);IC cat{{"pic",image}};auto h=q2c_cases::document(el("a","",dw,dh,"pic"));unsigned rx=dw-32,ry=dh-32;
  for(auto off:std::vector<tt::ViewportOffset>{{0,0},{rx/2,ry/2},{rx,ry}}){auto v=good(h,{"a"},b,32,32,cat,off,{0,int(dh*64)});assert(v->viewport.range_x==rx&&v->viewport.range_y==ry);assert(v->viewport.document_extent_w==dw&&v->viewport.document_extent_h==dh);assert(v->viewport.offset.x==off.x&&v->viewport.offset.y==off.y);chrome(v,32,32);
   // Fixed chrome is above translated content. Probe fully covered thumb centers,
   // avoiding rounded-edge coverage, and the rectangular corner independently.
   if(rx)assert((pixel(v,12,27)==std::array<unsigned char,4>{over(128,140,12+off.x),over(128,140,27+off.y),over(128,140,77),255}));
   if(ry)assert((pixel(v,27,12)==std::array<unsigned char,4>{over(128,140,27+off.x),over(128,140,12+off.y),over(128,140,77),255}));
   if(rx&&ry)assert((pixel(v,27,27)==std::array<unsigned char,4>{over(128,71,27+off.x),over(128,71,27+off.y),over(128,71,77),255}));
   assert(v->scene.targets.size()==1&&v->scene.targets[0].resource==image);rect(v->scene.targets[0].crop,0,0,dw,dh);rect(v->scene.targets[0].destination,0,0,dw,dh);assert((pixel(v,1,1)==std::array<unsigned char,4>{static_cast<unsigned char>(1+off.x),static_cast<unsigned char>(1+off.y),77,255}));
   // Margin outside fixed overlay remains content, not a reduced scrollport.
   assert((pixel(v,31,31)==std::array<unsigned char,4>{static_cast<unsigned char>(31+off.x),static_cast<unsigned char>(31+off.y),77,255}));++fixtures;}
  bad(h,{"a"},b,32,32,cat,{rx+1,0},tt::HtmlTextError::invalid_input,{0,int(dh*64)});bad(h,{"a"},b,32,32,cat,{0,ry+1},tt::HtmlTextError::invalid_input,{0,int(dh*64)});
 }
 auto narrow=q2c_cases::document(el("a",std::string(128,'A'),128,64)+el("b",std::string(128,'A'),128,64));
 auto legacy=tt::render_real_html_blocks_images(narrow,{"a","b"},b,{0,4096},256,256,{});assert(std::holds_alternative<tt::HtmlTextError>(legacy)&&std::get<tt::HtmlTextError>(legacy)==tt::HtmlTextError::preparation_failed);
 auto start=good(narrow,{"b","a"},b,256,256);assert(start->viewport.scrolling&&start->viewport.range_x==944&&start->viewport.range_y==0);assert(start->scene.blocks.owners[0].run->glyphs.size()==128&&start->scene.blocks.owners[1].run->glyphs.size()==128);assert(start->scene.blocks.owners[0].run->width_26_6==76800);auto end=good(narrow,{"a","b"},b,256,256,{}, {start->viewport.range_x,start->viewport.range_y});assert(end->scene.blocks.work.normalized_scalars==256);assert(end->scene.blocks.work.cmap_reserved==4352&&end->scene.blocks.work.outline_reserved==256000000);assert(end->scene.blocks.work.raster_reserved==start->scene.blocks.work.raster_reserved);assert(end->scene.blocks.owners[0].id=="a"&&end->scene.blocks.owners[1].id=="b");chrome(end,256,256);++fixtures;
 auto too=q2c_cases::document(el("a",std::string(129,'A'),128,64)+el("b",std::string(128,'A'),128,64));bad(too,{"a","b"},b,256,256,{}, {},tt::HtmlTextError::limit_exceeded);
 auto nooverflow=q2c_cases::document(el("a","",1,1));auto smallplain=good(nooverflow,{"a"},b,7,7,{}, {},{0,64});assert(!smallplain->viewport.scrolling);auto smallold=tt::render_real_html_blocks_images(nooverflow,{"a"},b,{0,64},7,7,{});assert(std::holds_alternative<std::shared_ptr<const tt::HtmlBlocksImagesFrame>>(smallold));assert(smallplain->scene.blocks.frame.pixels==std::get<std::shared_ptr<const tt::HtmlBlocksImagesFrame>>(smallold)->blocks.frame.pixels);++fixtures;
 auto tiny=q2c_cases::document(el("a","",16,16,"pic"));IC small{{"pic",im(16,16)}};bad(tiny,{"a"},b,7,8,small,{},tt::HtmlTextError::unsupported,{0,1024});bad(tiny,{"a"},b,8,7,small,{},tt::HtmlTextError::unsupported,{0,1024});auto eight=good(tiny,{"a"},b,8,8,small,{}, {0,1024});chrome(eight,8,8);++fixtures;
 // Actual two-owner scrolling fixture for observed-site failure injection.
 auto h=q2c_cases::document(el("a","A",64,64,"first")+el("b","B",64,64,"last"));IC cat{{"first",im(64,64)},{"last",im(64,64)}};std::vector<std::string> ids{"b","a"};auto stable=good(h,ids,b,32,32,cat,{16,70});auto pixels=stable->scene.blocks.frame.pixels;auto res=stable->scene.targets.back().resource;assert(stable->scene.targets.size()==2);rect(stable->scene.targets[0].destination,0,0,64,64);rect(stable->scene.targets[1].destination,0,64,64,64);rect(stable->scene.targets[0].crop,0,0,64,64);assert((pixel(stable,15,15)==std::array<unsigned char,4>{31,21,77,255}));++fixtures;
 // First image is completely above viewport, second partially visible; full document geometry survives.
 std::weak_ptr<const ip::ImageResource> weak;VF retained;{auto local=im(64,64);weak=local;IC owned{{"owned",local}};retained=good(q2c_cases::document(el("a","",64,64,"owned")),{"a"},b,32,32,owned,{16,16});owned.clear();local.reset();assert(!weak.expired());}assert((pixel(retained,1,1)==std::array<unsigned char,4>{17,17,77,255}));retained.reset();assert(weak.expired());++fixtures;

 Cancel probe(~0u);assert(std::holds_alternative<VF>(tt::render_real_html_viewport(h,ids,b,{0,4096},32,32,cat,{16,70},&probe)));unsigned canceled=0;for(unsigned i=1;i<=probe.calls;++i){Cancel c(i);auto r=tt::render_real_html_viewport(h,ids,b,{0,4096},32,32,cat,{16,70},&c);assert(std::holds_alternative<tt::HtmlTextError>(r)&&std::get<tt::HtmlTextError>(r)==tt::HtmlTextError::cancelled);++canceled;assert(stable->scene.blocks.frame.pixels==pixels&&stable->scene.targets.back().resource==res);}
 allocation_count=0;auto pr=tt::render_real_html_viewport(h,ids,b,{0,4096},32,32,cat,{16,70});auto count=allocation_count;assert(std::holds_alternative<VF>(pr));unsigned ooms=0;for(std::size_t i=0;i<count;++i){fail_after=i;auto r=tt::render_real_html_viewport(h,ids,b,{0,4096},32,32,cat,{16,70});fail_after=-1;if(auto e=std::get_if<tt::HtmlTextError>(&r)){assert(*e==tt::HtmlTextError::out_of_memory);++ooms;}assert(stable->scene.blocks.frame.pixels==pixels&&stable->scene.targets.back().resource==res);}
 legacy=tt::render_real_html_blocks_images(narrow,{"a","b"},b,{0,4096},256,256,{});assert(std::holds_alternative<tt::HtmlTextError>(legacy)&&std::get<tt::HtmlTextError>(legacy)==tt::HtmlTextError::preparation_failed);++fixtures;
 std::cout<<"Q2g public fixtures="<<fixtures<<" canceled="<<canceled<<" allocation_sites="<<count<<" typed_ooms="<<ooms<<" lastgood=intact legacy-root-rejection=intact prepared-lifetime=separate-private-gate\n";
}
