#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/text/real_html_blocks_images_admission.h"
#include "html_text_real_cases.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
namespace tt=aether::rendering::text::truetype;
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
static std::string bg(std::string key="hero"){return ";background-image:url("+key+");background-repeat:no-repeat;background-position:0px 0px;background-size:auto";}
static std::string owner(std::string id,std::string text,std::string css=q2c_cases::style()){return "<div id=\""+id+"\" style=\""+css+"\">"+text+"</div>";}
static auto run(const std::string& html,const std::vector<std::string>& ids={"aether-text"}){return tt::admit_real_html_blocks_images(html,ids,1024,{3072,4096});}
static void error(const std::string& html,tt::HtmlAdmissionError expected=tt::HtmlAdmissionError::unsupported){auto r=run(html);assert(std::holds_alternative<tt::HtmlAdmissionError>(r));assert(std::get<tt::HtmlAdmissionError>(r)==expected);}
static std::string change(std::string s,const std::string& from,const std::string& to){auto i=s.find(from);assert(i!=std::string::npos);s.replace(i,from.size(),to);return s;}
struct Cancel:tt::Cancellation {unsigned calls{},at{};bool cancelled()noexcept override{return ++calls>=at;}};
int main(){
 unsigned corpus=0;
 for(auto& c:q2c_cases::corpus()){auto r=run(c.html);if(c.accepted||c.name=="raw-style-over4096"){assert(std::holds_alternative<tt::HtmlBlocksImagesAdmission>(r));auto& p=std::get<tt::HtmlBlocksImagesAdmission>(r);assert(p.images.empty());}else assert(std::holds_alternative<tt::HtmlAdmissionError>(r));++corpus;}
 for(auto key:{"a","Hero_09-x","&#x61;","h&#101;ro"}){auto r=run(q2c_cases::document(owner("aether-text","AB",q2c_cases::style()+bg(key))));assert(std::holds_alternative<tt::HtmlBlocksImagesAdmission>(r));auto& p=std::get<tt::HtmlBlocksImagesAdmission>(r);assert(p.images.size()==1&&p.images[0].id=="aether-text"&&p.images[0].node_index==p.blocks.owners[0].node_index);}
 auto style=q2c_cases::style()+bg();auto good=q2c_cases::document(owner("aether-text","AB",style));
 for(auto key:{"_a","1a","","a/b","a:b","a.b","a%20b","a b","a()","&#47;evil","a&#58;b","&quot;a&quot;","a\\b","é"})error(q2c_cases::document(owner("aether-text","AB",q2c_cases::style()+bg(key))));
 auto maxkey=std::string(64,'a');assert(std::holds_alternative<tt::HtmlBlocksImagesAdmission>(run(q2c_cases::document(owner("aether-text","AB",q2c_cases::style()+bg(maxkey))))));error(q2c_cases::document(owner("aether-text","AB",q2c_cases::style()+bg(maxkey+"a"))));
 for(auto v:{"repeat","repeat-x","no-repeat no-repeat"})error(change(good,"background-repeat:no-repeat","background-repeat:"+std::string(v)));
 for(auto v:{"left top","0px  0px","1px 0px","0% 0%"})error(change(good,"background-position:0px 0px","background-position:"+std::string(v)));
 for(auto v:{"contain","auto auto","2px 2px","100%"})error(change(good,"background-size:auto","background-size:"+std::string(v)));
 for(auto prop:{"background-repeat:no-repeat;","background-position:0px 0px;","background-size:auto"})error(change(good,prop,""));
 for(auto prop:{"background-origin:border-box","background-clip:border-box","background:url(hero)","height:4px","width:calc(1px)","padding-top:url(hero)"})error(q2c_cases::document(owner("aether-text","AB",style+";"+prop)));
 error(change(good,"border-left-width:0px","border-left-width:1px"));error(change(good,"border-top-width:0px;",""));
 error(q2c_cases::document(owner("aether-text","AB",q2c_cases::style()+";background-repeat:no-repeat")));
 error(q2c_cases::document(owner("aether-text","AB",style)+"<div id=\"second\" style=\"display:block;border-top-width:0px;border-right-width:0px;border-bottom-width:0px;border-left-width:0px"+bg()+"\"></div>"));
 std::string wrapper="display:block;border-top-width:0px;border-right-width:0px;border-bottom-width:0px;border-left-width:0px";
 error(q2c_cases::document(owner("wrapper",owner("aether-text","AB",style),wrapper+bg())));
 auto nested=run(q2c_cases::document(owner("wrapper",owner("aether-text","AB",style),wrapper+bg("other"))));assert(std::holds_alternative<tt::HtmlBlocksImagesAdmission>(nested));auto& np=std::get<tt::HtmlBlocksImagesAdmission>(nested);assert(np.images.size()==2&&np.images[0].id=="wrapper"&&np.images[1].id=="aether-text");
 std::string eight=owner("aether-text","AB",style);for(int i=1;i<8;++i)eight+=owner("i"+std::to_string(i),"",wrapper+bg("key"+std::to_string(i)));
 assert(std::get<tt::HtmlBlocksImagesAdmission>(run(q2c_cases::document(eight))).images.size()==8);
 error(q2c_cases::document(eight+owner("ninth","",wrapper+bg("key9"))),tt::HtmlAdmissionError::limit_exceeded);
 // The opt-in scanner never widens the old Q2e admission entry.
 auto old=tt::admit_real_html_blocks(good,{"aether-text"},1024,{3072,4096});assert(std::get<tt::HtmlAdmissionError>(old)==tt::HtmlAdmissionError::unsupported);
 unsigned failures=0;const std::vector<std::string> ids{"aether-text"};
 auto baseline=std::get<tt::HtmlBlocksImagesAdmission>(run(good,ids));
 auto offset=tt::admit_real_html_blocks_images(good,ids,1024,{3072,4096},nullptr,123);
 assert(std::holds_alternative<tt::HtmlBlocksImagesAdmission>(offset));assert(std::get<tt::HtmlBlocksImagesAdmission>(offset).blocks.logical_bytes==baseline.blocks.logical_bytes+123);
 auto full=tt::admit_real_html_blocks_images(good,ids,1024,{3072,4096},nullptr,2*1024*1024);assert(std::get<tt::HtmlAdmissionError>(full)==tt::HtmlAdmissionError::limit_exceeded);
 fail_after=0;auto over=tt::admit_real_html_blocks_images(good,ids,1024,{3072,4096},nullptr,2*1024*1024+1);fail_after=-1;assert(std::get<tt::HtmlAdmissionError>(over)==tt::HtmlAdmissionError::limit_exceeded);

 for(long i=0;i<10000;++i){fail_after=i;auto r=run(good,ids);fail_after=-1;if(std::holds_alternative<tt::HtmlBlocksImagesAdmission>(r))break;assert(std::get<tt::HtmlAdmissionError>(r)==tt::HtmlAdmissionError::out_of_memory);++failures;}assert(failures>100);
 unsigned cancelled=0;for(unsigned at=1;at<10000;++at){Cancel c;c.at=at;auto r=tt::admit_real_html_blocks_images(good,ids,1024,{3072,4096},&c);if(std::holds_alternative<tt::HtmlBlocksImagesAdmission>(r))break;assert(std::get<tt::HtmlAdmissionError>(r)==tt::HtmlAdmissionError::cancelled);++cancelled;}assert(cancelled>10);
 std::cout<<"Q2f admission inherited_corpus="<<corpus<<" image token/CSS/8targets/nested-key/cap/oldAPI checks PASS; typed_oom="<<failures<<" cancellation_points="<<cancelled<<"\n";
}
