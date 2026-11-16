#include "real_html_admission.h"
#include "../../dom/dom_types.h"
#include <array>
#include <limits>
#include <new>
#include <set>
namespace aether::rendering::text::truetype {
namespace {
using AdmissionError=HtmlAdmissionError;
bool space(char c) { return c==' '||c=='\t'||c=='\n'||c=='\f'; }
char lower(char c) { return c>='A'&&c<='Z'?char(c+('a'-'A')):c; }
bool letter(char c) { c=lower(c);return c>='a'&&c<='z'; }
struct Scanner {
 std::string_view raw; std::size_t pos{},ticks{},style_bytes{},raw_style_bytes{},decls{}; std::int32_t font; LineBox box; Cancellation* cancel;
 HtmlAdmission proof; std::vector<std::size_t> stack; std::size_t owners{}; std::set<std::string> ids;
 void check() { if(cancel&&cancel->cancelled())throw AdmissionError::cancelled; }
 // Poll explicit byte/validation loops; bounded library find/equality calls are synchronous.
 std::string_view trimmed(std::string_view s) { while(!s.empty()&&space(s.front())){step();s.remove_prefix(1);}while(!s.empty()&&space(s.back())){step();s.remove_suffix(1);}return s; }
 void step() { if((++ticks&127)==0)check(); }
 char take() { if(pos==raw.size())throw AdmissionError::invalid_input;step();return raw[pos++]; }
 void ws() { while(pos<raw.size()&&space(raw[pos]))take(); }
 std::string name() { std::string s;while(pos<raw.size()&&(letter(raw[pos])||raw[pos]=='-'))s+=lower(take());if(s.empty())throw AdmissionError::invalid_input;return s; }
 static void utf8(std::string& s,std::uint32_t c) { if(c<128)s+=char(c);else if(c<2048){s+=char(192|(c>>6));s+=char(128|(c&63));}else if(c<65536){s+=char(224|(c>>12));s+=char(128|((c>>6)&63));s+=char(128|(c&63));}else{s+=char(240|(c>>18));s+=char(128|((c>>12)&63));s+=char(128|((c>>6)&63));s+=char(128|(c&63));} }
 std::string decode(std::string_view s) {
  std::string out;
  for(std::size_t i=0;i<s.size();) {
   step();unsigned char c=static_cast<unsigned char>(s[i++]);
   if(c!='&') {
    if(c<128){if(!c||c=='\r')throw AdmissionError::invalid_input;out+=char(c);continue;}
    std::uint32_t cp{};unsigned n{};std::uint32_t minimum{};
    if(c>=194&&c<=223){cp=c&31;n=1;minimum=128;}else if(c>=224&&c<=239){cp=c&15;n=2;minimum=2048;}else if(c>=240&&c<=244){cp=c&7;n=3;minimum=65536;}else throw AdmissionError::invalid_input;
    if(n>s.size()-i)throw AdmissionError::invalid_input;
    for(unsigned j=0;j<n;++j){auto b=static_cast<unsigned char>(s[i++]);if((b&192)!=128)throw AdmissionError::invalid_input;cp=(cp<<6)|(b&63);}
    if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))throw AdmissionError::invalid_input;
    utf8(out,cp);continue;
   }
   auto start=i;while(i<s.size()&&s[i]!=';'){step();if(i-start>=16)throw AdmissionError::invalid_input;++i;}
   if(i==s.size())throw AdmissionError::invalid_input;
   auto entity=s.substr(start,i++-start);std::uint32_t cp=0;
   if(entity=="amp")cp=38;else if(entity=="lt")cp=60;else if(entity=="gt")cp=62;else if(entity=="quot")cp=34;else if(entity=="apos")cp=39;else if(entity=="nbsp")cp=160;
   else if(!entity.empty()&&entity.front()=='#') {
    entity.remove_prefix(1);unsigned base=10;if(!entity.empty()&&(entity.front()=='x'||entity.front()=='X')){base=16;entity.remove_prefix(1);}if(entity.empty())throw AdmissionError::invalid_input;
    for(char d:entity){unsigned digit=d>='0'&&d<='9'?unsigned(d-'0'):lower(d)>='a'&&lower(d)<='f'?unsigned(lower(d)-'a'+10):99;if(digit>=base||cp>(0x10ffff-digit)/base)throw AdmissionError::invalid_input;cp=cp*base+digit;}
    if(!cp||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))throw AdmissionError::invalid_input;
   }else throw AdmissionError::unsupported;
   utf8(out,cp);
  }
  return out;
 }
 bool px(std::string_view s,std::int64_t target,bool integral=false) {
  if(s.size()<3||s.substr(s.size()-2)!="px")return false;
  s.remove_suffix(2);if(s.empty())return false;
  std::int64_t whole=0,fraction=0,scale=1;bool dot=false,any=false,after=false;
  for(char c:s){step();if(c=='.'&&!dot&&!integral&&any){dot=true;continue;}if(c<'0'||c>'9')return false;any=true;if(dot){after=true;if(scale>=1000000)return false;scale*=10;fraction=fraction*10+c-'0';}else{if(whole>1000000)return false;whole=whole*10+c-'0';}}
  if(!any||(dot&&!after)||whole>1000000)return false;
  auto n=whole*64*scale+fraction*64;if(n%scale)return false;return target<0||n/scale==target;
 }
 bool color(std::string_view s) {if(s.size()!=7||s[0]!='#')return false;for(char c:s.substr(1)){step();if(!((c>='0'&&c<='9')||(lower(c)>='a'&&lower(c)<='f')))return false;}return true;}
 void styles(HtmlAdmissionNode& n,bool owner) {
  auto it=n.attributes.find("style");if(it==n.attributes.end())throw AdmissionError::unsupported;
  std::string_view s=it->second;style_bytes+=s.size();if(style_bytes>4096)throw AdmissionError::limit_exceeded;
  while(!trimmed(s).empty()) {
   s=trimmed(s);auto colon=s.find(':');if(colon==s.npos)throw AdmissionError::invalid_input;
   auto p=trimmed(s.substr(0,colon));std::string key;for(char c:p){step();if(!letter(c)&&c!='-')throw AdmissionError::unsupported;key+=lower(c);}if(key.empty())throw AdmissionError::invalid_input;
   s.remove_prefix(colon+1);auto semi=s.find(';');auto value=trimmed(s.substr(0,semi));if(value.empty())throw AdmissionError::invalid_input;
   for(char c:value){step();if(static_cast<unsigned char>(c)>=128||c=='\\'||c=='/'||c=='!'||c=='('||c==')'||c=='%'||c==':'||c=='\''||c=='"')throw AdmissionError::unsupported;}
   if(++decls>128)throw AdmissionError::limit_exceeded;
   if(!n.declarations.emplace(key,value).second)throw AdmissionError::unsupported;
   if(semi==s.npos){s={};}else s.remove_prefix(semi+1);
  }
  static constexpr std::pair<std::string_view,std::string_view> fixed[]={
   {"font-family","aether-explicit"},{"white-space","pre"},{"font-weight","normal"},{"font-style","normal"},{"font-stretch","normal"},{"font-kerning","none"},{"font-variant-ligatures","none"},{"direction","ltr"},{"writing-mode","horizontal-tb"},{"text-transform","none"},{"text-align","left"},{"text-indent","0px"},{"letter-spacing","0px"},{"word-spacing","0px"},{"color","#000000"}};
  for(auto& [key,v]:n.declarations) {
   step();
   bool valid=false;
   for(auto [p,w]:fixed){step();if(key==p){valid=owner&&v==w;break;}}
   if(key=="white-space") {valid=owner&&(v=="pre"||v=="nowrap");if(valid)proof.profile=v=="pre"?HtmlWhiteSpaceProfile::pre:HtmlWhiteSpaceProfile::nowrap;}
   if(key=="font-size")valid=owner&&px(v,font);
   if(key=="line-height")valid=owner&&px(v,box.height26_6);
   if(key=="display")valid=v=="block";
   if(key=="width")valid=px(v,-1,true);
   if(key=="height")valid=v=="auto";
   if(key=="background-color")valid=color(v);
   for(std::string_view side:{"top","right","bottom","left"}) {
    step();
    if(key==std::string("margin-")+std::string(side))valid=v=="0px";
    if(key==std::string("padding-")+std::string(side))valid=px(v,-1,true);
    auto prefix=std::string("border-")+std::string(side);
    if(key==prefix+"-width")valid=px(v,-1,true);
    if(key==prefix+"-style")valid=v=="solid";
    if(key==prefix+"-color")valid=color(v);
   }
   if(!valid)throw AdmissionError::unsupported;
  }
  if(owner){for(auto [p,w]:fixed){step();(void)w;if(!n.declarations.count(p))throw AdmissionError::unsupported;}if(!n.declarations.count("font-size")||!n.declarations.count("line-height"))throw AdmissionError::unsupported;}
  if(n.tag=="html"||n.tag=="body") {
   if(n.declarations.find("display")==n.declarations.end()||n.declarations.at("display")!="block")throw AdmissionError::unsupported;
   for(std::string_view side:{"top","right","bottom","left"})for(auto prefix:{"margin-","padding-","border-"}) {
    step();auto k=std::string(prefix)+std::string(side)+(std::string_view(prefix)=="border-"?"-width":"");auto d=n.declarations.find(k);if(d==n.declarations.end()||d->second!="0px")throw AdmissionError::unsupported;
   }
  }
 }
 std::size_t append(HtmlAdmissionNode n) {
  if(proof.nodes.size()>=128)throw AdmissionError::limit_exceeded;
  auto index=proof.nodes.size();proof.nodes.push_back(std::move(n));if(stack.empty())proof.roots.push_back(index);else proof.nodes[stack.back()].children.push_back(index);return index;
 }
 HtmlAdmission run() {
  check();if(raw.size()>65536)throw AdmissionError::limit_exceeded;
  for(char c:raw){step();if(!c||c=='\r')throw AdmissionError::invalid_input;}
  if(raw.substr(0,15)=="<!DOCTYPE html>")pos=15;
  while(pos<raw.size()) {
   check();
   if(raw[pos]!='<') {
    auto start=pos;while(pos<raw.size()&&raw[pos]!='<')take();auto text=decode(raw.substr(start,pos-start));
    if(stack.empty()||stack.back()!=proof.owner||owners!=1)throw AdmissionError::unsupported;
    if(text.size()>4096-proof.decoded_text.size())throw AdmissionError::limit_exceeded;
    // Enforce the exact simple-LTR repertoire per individual piece as well as joined.
    for(std::size_t i=0;i<text.size();) {step();auto c=static_cast<unsigned char>(text[i++]);unsigned cp=c;if(c>=128){if(c!=0xc2&&c!=0xc3)throw AdmissionError::unsupported;cp=((c&31)<<6)|(static_cast<unsigned char>(text[i++])&63);}if(!((cp>=32&&cp<=126)||(cp>=160&&cp<=255&&cp!=173)||(proof.profile==HtmlWhiteSpaceProfile::nowrap&&(cp==9||cp==10))))throw AdmissionError::unsupported;}
    proof.decoded_text+=text;proof.pieces.push_back(text);HtmlAdmissionNode n;n.kind=HtmlAdmissionNode::Kind::text;n.text=std::move(text);append(std::move(n));continue;
   }
   if(raw.substr(pos,4)=="<!--") {
    for(int i=0;i<4;++i)take();
    auto start=pos;while(pos<raw.size()&&raw.substr(pos,3)!="-->")take();if(pos==raw.size())throw AdmissionError::invalid_input;
    auto value=raw.substr(start,pos-start);if(value.find("--")!=value.npos||(!value.empty()&&(value.front()=='>'||value.back()=='-'))||value.substr(0,2)=="->")throw AdmissionError::invalid_input;
    HtmlAdmissionNode n;n.kind=HtmlAdmissionNode::Kind::comment;n.text=std::string(value);append(std::move(n));if(owners==1&&!stack.empty()&&stack.back()==proof.owner)proof.pieces.emplace_back();for(int i=0;i<3;++i)take();continue;
   }
   take();if(pos<raw.size()&&raw[pos]=='/') {
    take();auto tag=name();ws();if(take()!='>'||stack.empty()||proof.nodes[stack.back()].tag!=tag)throw AdmissionError::invalid_input;stack.pop_back();continue;
   }
   HtmlAdmissionNode n;n.tag=name();if(n.tag!="html"&&n.tag!="body"&&n.tag!="div")throw AdmissionError::unsupported;
   if(stack.empty()){if(n.tag!="html"||!proof.roots.empty())throw AdmissionError::unsupported;}
   else {auto& parent=proof.nodes[stack.back()];if((owners&&stack.back()==proof.owner)||(n.tag=="html")||(parent.tag=="html"&&n.tag!="body")||(parent.tag!="html"&&n.tag!="div"))throw AdmissionError::unsupported;}
   while(true){auto before=pos;ws();if(pos==raw.size())throw AdmissionError::invalid_input;if(raw[pos]=='>'){take();break;}if(before==pos)throw AdmissionError::invalid_input;auto key=name();if(key!="style"&&key!="id")throw AdmissionError::unsupported;ws();if(take()!='=')throw AdmissionError::invalid_input;ws();char quote=take();if(quote!='\''&&quote!='"')throw AdmissionError::unsupported;auto start=pos;while(pos<raw.size()&&raw[pos]!=quote)take();if(pos==raw.size())throw AdmissionError::invalid_input;if(key=="style"){raw_style_bytes+=pos-start;if(raw_style_bytes>4096)throw AdmissionError::limit_exceeded;}auto value=decode(raw.substr(start,pos-start));take();if(!n.attributes.emplace(key,std::move(value)).second)throw AdmissionError::unsupported;}
   if(auto id=n.attributes.find("id");id!=n.attributes.end()&&!ids.insert(id->second).second)throw AdmissionError::unsupported;
   bool owner=n.attributes.count("id")&&n.attributes.at("id")=="aether-text";
   if(owner&&(n.tag!="div"||++owners!=1))throw AdmissionError::unsupported;
   styles(n,owner);auto index=append(std::move(n));if(owner)proof.owner=index;if(stack.size()>=16)throw AdmissionError::limit_exceeded;stack.push_back(index);
  }
  if(!stack.empty()||owners!=1||proof.roots.size()!=1)throw AdmissionError::invalid_input;
  auto& root=proof.nodes[proof.roots[0]];if(root.children.size()!=1||proof.nodes[root.children[0]].tag!="body")throw AdmissionError::unsupported;
  check();return std::move(proof);
 }
};
bool match(const HtmlAdmission& p,std::size_t i,aether_node_s* node,aether_node_s*& owner,std::size_t depth,std::size_t& count) {
 if(!node||i>=p.nodes.size()||depth>16||++count>128||node->is_generated||node->anonymous_flex_owner)return false;
 const auto& n=p.nodes[i];auto type=n.kind==HtmlAdmissionNode::Kind::element?AETHER_NODE_ELEMENT:n.kind==HtmlAdmissionNode::Kind::text?AETHER_NODE_TEXT:AETHER_NODE_COMMENT;
 if(node->type!=type||node->tag_name!=n.tag||node->text!=n.text||node->attributes!=n.attributes||node->children.size()!=n.children.size())return false;
 if(i==p.owner)owner=node;
 for(std::size_t j=0;j<n.children.size();++j)if(!node->children[j]||node->children[j]->parent!=node||!match(p,n.children[j],node->children[j],owner,depth+1,count))return false;
 return true;
}
}
AdmissionResult admit_real_html(std::string_view raw,std::int32_t font,LineBox box,Cancellation* cancel) noexcept {
 try {if(font<=0||font>8192||box.height26_6<=0||box.height26_6>8192*64||box.baseline26_6<0||box.baseline26_6>box.height26_6)return AdmissionError::invalid_input;return Scanner{raw,0,0,0,0,0,font,box,cancel,{},{},0,{}}.run();}
 catch(AdmissionError e){return e;}catch(const std::bad_alloc&){return AdmissionError::out_of_memory;}catch(...){return AdmissionError::invalid_input;}
}
aether_node_s* verify_real_html_dom(const HtmlAdmission& p,aether_doc_s* doc) noexcept {
 if(!doc||p.nodes.empty()||p.nodes.size()>128||doc->children.size()!=p.roots.size()||p.roots.size()!=1)return nullptr;
 aether_node_s* owner=nullptr;std::size_t count=0;for(std::size_t i=0;i<p.roots.size();++i)if(!doc->children[i]||doc->children[i]->parent!=doc||!match(p,p.roots[i],doc->children[i],owner,0,count))return nullptr;
 return count==p.nodes.size()?owner:nullptr;
}
}
