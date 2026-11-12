#include <aether/rendering/preparation/detail/utf8.hpp>
#include "aether_visual_compare/resource_manifest.hpp"
#include <algorithm>
#include <charconv>
#include <map>
#include <set>
#include <stdexcept>
namespace avc {
bool valid_utf8(std::string_view s) {
 return aether::rendering::preparation::detail::valid_utf8(s);
}
bool valid_resource_path(std::string_view path) {
 if(path.empty() || path.size()>4096) return false;
 std::size_t start=0;
 while(start<path.size()) {
  auto end=path.find('/',start); if(end==path.npos) end=path.size();
  auto part=path.substr(start,end-start);
  if(part.empty() || part=="." || part==".." || part.back()=='.') return false;
  std::string stem;
  for(char c:part) {
   if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.')) return false;
  }
  auto dot=part.find('.'); auto base=part.substr(0,dot);
  for(char c:base) stem+=c>='a'&&c<='z'?static_cast<char>(c-'a'+'A'):c;
  if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL"||stem=="CLOCK$") return false;
  if(stem.size()==4 && (stem.substr(0,3)=="COM"||stem.substr(0,3)=="LPT") && stem[3]>='0'&&stem[3]<='9') return false;
  if(end==path.size()) return true;
  start=end+1;
 }
 return false;
}
namespace {
struct Invalid {};
class Parser {
 std::string_view input; std::size_t p=0;
 [[noreturn]] void fail() const { throw Invalid{}; }
 void ws() { while(p<input.size() && (input[p]==' '||input[p]=='\t'||input[p]=='\r'||input[p]=='\n')) ++p; }
 bool take(char c) { if(p<input.size()&&input[p]==c) {++p;return true;} return false; }
 unsigned hex4() {
  unsigned n=0;
  for(int i=0;i<4;++i) { if(p==input.size()) fail(); char c=input[p++]; unsigned v;
   if(c>='0'&&c<='9') v=c-'0'; else if(c>='a'&&c<='f') v=c-'a'+10; else if(c>='A'&&c<='F') v=c-'A'+10; else fail(); n=(n<<4)|v;
  } return n;
 }
 void utf8(std::string& s,unsigned cp) {
  if(cp<0x80) s+=static_cast<char>(cp);
  else if(cp<0x800) {s+=static_cast<char>(0xc0|(cp>>6));s+=static_cast<char>(0x80|(cp&63));}
  else if(cp<0x10000) {s+=static_cast<char>(0xe0|(cp>>12));s+=static_cast<char>(0x80|((cp>>6)&63));s+=static_cast<char>(0x80|(cp&63));}
  else {s+=static_cast<char>(0xf0|(cp>>18));s+=static_cast<char>(0x80|((cp>>12)&63));s+=static_cast<char>(0x80|((cp>>6)&63));s+=static_cast<char>(0x80|(cp&63));}
 }
 std::string string() {
  if(!take('"')) fail();
  std::string s;
  for(;;) {
   if(p==input.size()) fail();
   auto c=static_cast<unsigned char>(input[p++]);
   if(c=='"') return s;
   if(c<32) fail();
   if(c!='\\') s+=static_cast<char>(c);
   else {
    if(p==input.size()) fail();
    char e=input[p++];
    switch(e) {
     case '"': s+='"';break; case '\\':s+='\\';break;case '/':s+='/';break;
     case 'b':s+='\b';break;case 'f':s+='\f';break;case 'n':s+='\n';break;case 'r':s+='\r';break;case 't':s+='\t';break;
     case 'u': {
      auto cp=hex4();
      if(cp>=0xd800&&cp<=0xdbff) {if(!take('\\')||!take('u')) fail(); auto lo=hex4();if(lo<0xdc00||lo>0xdfff) fail();cp=0x10000+((cp-0xd800)<<10)+(lo-0xdc00);}
      else if(cp>=0xdc00&&cp<=0xdfff) fail();
      utf8(s,cp);break;
     }
     default:fail();
    }
   }
   if(s.size()>4096) fail();
  }
 }
 Json value(unsigned depth) {
  if(depth>8) fail();
  ws();
  if(p==input.size()) fail();
  if(take('{')) {
   Json v{Json::Kind::object, {}, {}, {}};ws();if(take('}'))return v;
   for(;;) {ws();auto k=string();ws();if(!take(':'))fail();auto x=value(depth+1);if(!v.object.emplace(std::move(k),std::move(x)).second)fail();ws();if(take('}'))return v;if(!take(','))fail();}
  }
  if(take('[')) {
   Json v{Json::Kind::array, {}, {}, {}};ws();if(take(']'))return v;
   for(;;) {v.array.push_back(value(depth+1));ws();if(take(']'))return v;if(!take(','))fail();}
  }
  if(input[p]=='"') {Json v{Json::Kind::string, {}, {}, {}};v.scalar=string();return v;}
  for(auto literal:{std::string_view("true"),std::string_view("false"),std::string_view("null")}) {
   if(input.substr(p,literal.size())==literal) {p+=literal.size();Json v{literal=="null"?Json::Kind::null_value:Json::Kind::boolean, {}, {}, {}};v.scalar=std::string(literal);return v;}
  }
  const auto begin=p;take('-');
  if(!take('0')) {if(p==input.size()||input[p]<'1'||input[p]>'9')fail();while(p<input.size()&&input[p]>='0'&&input[p]<='9')++p;}
  if(take('.')) {auto b=p;while(p<input.size()&&input[p]>='0'&&input[p]<='9')++p;if(b==p)fail();}
  if(take('e')||take('E')) {if(!take('+'))take('-');auto b=p;while(p<input.size()&&input[p]>='0'&&input[p]<='9')++p;if(b==p)fail();}
  Json v{Json::Kind::number, {}, {}, {}};v.scalar=std::string(input.substr(begin,p-begin));return v;
 }
public:
 explicit Parser(std::string_view s):input(s){}
 Json parse() {auto v=value(1);ws();if(p!=input.size())fail();return v;}
};
const std::string& string_field(const Json& v,const char* key) {
 auto it=v.object.find(key);if(it==v.object.end()||it->second.kind!=Json::Kind::string)throw Invalid{};return it->second.scalar;
}
}
const Json* Json::get(std::string_view key) const {
 auto it=object.find(std::string(key));return it==object.end()?nullptr:&it->second;
}
std::optional<Json> parse_json(std::string_view input) {
 if(input.size()>1024*1024 || !valid_utf8(input)) return std::nullopt;
 try { return Parser(input).parse(); } catch(const Invalid&) { return std::nullopt; }
}
std::optional<Manifest> parse_manifest(std::string_view input) {
 if(input.size()>1024*1024 || !valid_utf8(input)) return std::nullopt;
 try {
  const auto root=Parser(input).parse();
  if(root.kind!=Json::Kind::object || root.object.size()!=2 || string_field(root,"schema_version")!="aether.resources/1") throw Invalid{};
  auto e=root.object.find("entries");if(e==root.object.end()||e->second.kind!=Json::Kind::array||e->second.array.size()>1024)throw Invalid{};
  Manifest result;std::set<std::string> paths;
  const std::set<std::string> mimes{"text/css","image/png","image/jpeg","image/gif","image/svg+xml","font/ttf","font/otf","font/woff","font/woff2","application/octet-stream"};
  const std::set<std::string> roles{"stylesheet","image","font","other"};
  for(const auto& v:e->second.array) {
   if(v.kind!=Json::Kind::object||v.object.size()!=5)throw Invalid{};
   Resource r;r.path=string_field(v,"path");r.mime=string_field(v,"mime");r.sha256=string_field(v,"sha256");r.role=string_field(v,"role");
   if(!valid_resource_path(r.path)||!paths.insert(r.path).second||!mimes.contains(r.mime)||!roles.contains(r.role))throw Invalid{};
   if(r.sha256.size()!=64||!std::all_of(r.sha256.begin(),r.sha256.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');}))throw Invalid{};
   auto n=v.object.find("size_bytes");if(n==v.object.end()||n->second.kind!=Json::Kind::number)throw Invalid{};
   const auto& s=n->second.scalar;if(s.empty()||!std::all_of(s.begin(),s.end(),[](char c){return c>='0'&&c<='9';}))throw Invalid{};
   const auto parsed=std::from_chars(s.data(),s.data()+s.size(),r.size_bytes);
   if(parsed.ec!=std::errc{}||parsed.ptr!=s.data()+s.size()||r.size_bytes>16*1024*1024)throw Invalid{};
   result.entries.push_back(std::move(r));
  }
  return result;
 } catch(const Invalid&) {return std::nullopt;}
}
}
