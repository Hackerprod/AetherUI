#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/text/real_html_blocks_admission.h"
#include "html_text_real_cases.h"
#include "../src/dom/dom_types.h"
#include <cassert>
#include <iostream>
#include <cstdlib>
#include <new>
namespace tt=aether::rendering::text::truetype;
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
static std::string owner(std::string id,std::string text,std::string css=q2c_cases::style()){return "<div id=\""+id+"\" style=\""+css+"\">"+text+"</div>";}
static auto run(const std::string& html,const std::vector<std::string>& ids){return tt::admit_real_html_blocks(html,ids,1024,{3072,4096});}
static void error(const std::string& html,const std::vector<std::string>& ids,tt::HtmlAdmissionError e){auto r=run(html,ids);assert(std::holds_alternative<tt::HtmlAdmissionError>(r));assert(std::get<tt::HtmlAdmissionError>(r)==e);}
struct Cancel : tt::Cancellation { unsigned calls{},at{}; bool cancelled() noexcept override{return ++calls>=at;} };
int main(){
 unsigned fixtures=0;
 for(auto& c:q2c_cases::corpus()){auto r=run(c.html,{"aether-text"});if(c.accepted){assert(std::holds_alternative<tt::HtmlBlocksAdmission>(r));assert(std::get<tt::HtmlBlocksAdmission>(r).owners[0].decoded_text==c.decoded);}else if(c.name=="raw-style-over4096")assert(std::holds_alternative<tt::HtmlBlocksAdmission>(r));else assert(std::holds_alternative<tt::HtmlAdmissionError>(r));++fixtures;}
 for(unsigned count:{1u,2u,4u,8u}){
  std::string inner;std::vector<std::string> ids;
  for(unsigned i=0;i<count;++i){auto id="o"+std::to_string(i);inner+=owner(id,std::to_string(i)+"<!--x-->&nbsp;");ids.insert(ids.begin(),id);}
  auto r=run(q2c_cases::document(inner),ids);assert(std::holds_alternative<tt::HtmlBlocksAdmission>(r));const auto& p=std::get<tt::HtmlBlocksAdmission>(r);assert(p.owners.size()==count&&p.steps>0&&p.steps<=1000000&&p.logical_bytes>0&&p.logical_bytes<2*1024*1024);
  for(unsigned i=0;i<count;++i){assert(p.owners[i].id=="o"+std::to_string(i));assert(p.owners[i].pieces.size()==3);assert(p.owners[i].pieces[1].empty());assert(p.nodes[p.owners[i].node_index].attributes.at("id")==p.owners[i].id);}
 }
 auto good=q2c_cases::document(owner("a","A")+owner("b","B"));
 error(good,{},tt::HtmlAdmissionError::invalid_input);error(good,{"a","a"},tt::HtmlAdmissionError::invalid_input);
 for(auto id:{"","1a","a b","a.b","a:b","é"})error(good,{id},tt::HtmlAdmissionError::invalid_input);
 error(good,{std::string(65,'a')},tt::HtmlAdmissionError::invalid_input);
 error(good,{"a","b","c","d","e","f","g","h","i"},tt::HtmlAdmissionError::limit_exceeded);
 error(good,{"a","missing"},tt::HtmlAdmissionError::unsupported);
 error(q2c_cases::document(owner("a",owner("b","B"))),{"a","b"},tt::HtmlAdmissionError::unsupported);
 error(q2c_cases::document(owner("a","A")+" "+owner("b","B")),{"a","b"},tt::HtmlAdmissionError::unsupported);
 error(q2c_cases::document(owner("a","A")+owner("a","B")),{"a"},tt::HtmlAdmissionError::unsupported);
 assert(std::holds_alternative<tt::HtmlBlocksAdmission>(run(q2c_cases::document(owner("a",std::string(2048,'a'))+owner("b",std::string(2048,'b'))),{"a","b"})));
 error(q2c_cases::document(owner("a",std::string(2048,'a'))+owner("b",std::string(2049,'b'))),{"a","b"},tt::HtmlAdmissionError::limit_exceeded);
 auto css=q2c_cases::style();css.replace(css.find("white-space:pre"),15,"white-space:nowrap");
 auto mixed=run(q2c_cases::document(owner("a"," A  ")+owner("b","\tB\n",css)),{"b","a"});assert(std::holds_alternative<tt::HtmlBlocksAdmission>(mixed));auto& p=std::get<tt::HtmlBlocksAdmission>(mixed);assert(p.owners[0].decoded_text==" A  "&&p.owners[1].decoded_text=="\tB\n");assert(p.owners[0].profile==tt::HtmlWhiteSpaceProfile::pre&&p.owners[1].profile==tt::HtmlWhiteSpaceProfile::nowrap);
 std::vector<std::string> ids{"a","b"};unsigned failures=0;
 for(long i=0;i<10000;++i){fail_after=i;auto r=run(good,ids);fail_after=-1;if(std::holds_alternative<tt::HtmlBlocksAdmission>(r))break;assert(std::get<tt::HtmlAdmissionError>(r)==tt::HtmlAdmissionError::out_of_memory);++failures;}
 assert(failures>100);
 auto admitted=std::get<tt::HtmlBlocksAdmission>(run(good,ids));
 aether_doc_s doc;std::vector<std::unique_ptr<aether_node_s>> nodes;
 for(const auto& n:admitted.nodes){auto d=std::make_unique<aether_node_s>();d->type=n.kind==tt::HtmlAdmissionNode::Kind::element?AETHER_NODE_ELEMENT:n.kind==tt::HtmlAdmissionNode::Kind::text?AETHER_NODE_TEXT:AETHER_NODE_COMMENT;d->tag_name=n.tag;d->text=n.text;d->attributes=n.attributes;nodes.push_back(std::move(d));}
 for(std::size_t i=0;i<admitted.nodes.size();++i)for(auto child:admitted.nodes[i].children){nodes[i]->children.push_back(nodes[child].get());nodes[child]->parent=nodes[i].get();}
 for(auto root:admitted.roots){doc.children.push_back(nodes[root].get());nodes[root]->parent=&doc;}
 auto mapping=tt::verify_real_html_blocks_nodes(admitted,&doc);assert(mapping.size()==nodes.size());for(std::size_t i=0;i<nodes.size();++i)assert(mapping[i]==nodes[i].get());
 auto mappedowners=tt::verify_real_html_blocks_dom(admitted,&doc);assert(mappedowners.size()==2&&mappedowners[0]==nodes[admitted.owners[0].node_index].get());
 mappedowners[1]->attributes["id"]="other";assert(tt::verify_real_html_blocks_nodes(admitted,&doc).empty());mappedowners[1]->attributes["id"]="b";
 auto parent=mappedowners[0]->parent;std::swap(parent->children[0],parent->children[1]);assert(tt::verify_real_html_blocks_nodes(admitted,&doc).empty());std::swap(parent->children[0],parent->children[1]);
 bool threw=false;fail_after=0;try{(void)tt::verify_real_html_blocks_nodes(admitted,&doc);}catch(const std::bad_alloc&){threw=true;}fail_after=-1;assert(threw);
 unsigned cancellations=0;for(unsigned at=1;at<10000;++at){Cancel c;c.at=at;auto r=tt::admit_real_html_blocks(good,ids,1024,{3072,4096},&c);if(std::holds_alternative<tt::HtmlBlocksAdmission>(r))break;assert(std::get<tt::HtmlAdmissionError>(r)==tt::HtmlAdmissionError::cancelled);++cancellations;}assert(cancellations>10);
 std::cout<<"DOM mapping/mutation/OOM verified; cancellations="<<cancellations<<"\n";
 std::cout<<"scanner corpus="<<fixtures<<" plus multi-owner boundaries; allocation failures="<<failures<<"\n";
}
