#include "../../native/src/layout/layout_types.h"
#include <string_view>
#include <utility>
#include <charconv>
#include <cmath>
namespace aether::layout {
namespace {
bool ascii_space(char c) {return c==' ' || c=='\t' || c=='\n' || c=='\r';}
std::string_view trim(std::string_view s) {
 while(!s.empty() && ascii_space(s.front())) s.remove_prefix(1);
 while(!s.empty() && ascii_space(s.back())) s.remove_suffix(1);
 return s;
}
bool known_property(std::string_view s) {
 // A fixture grammar, NOT a CSS parser: no recovery, escapes, shorthand font,
 // custom properties, stylesheet imports or unrepresented text properties.
 constexpr std::string_view allowed[]={"display","font-family","font-size","font-weight",
  "white-space","direction","writing-mode","text-transform","text-align","line-height",
  "width","height","min-width","max-width","min-height","max-height",
  "margin","padding","color","background-color","opacity","position",
  "flex-direction","align-items","justify-content","flex-grow","flex-shrink"};
 for(auto property:allowed) if(s==property)return true;
 return false;
}
bool one_of(std::string_view value,std::initializer_list<std::string_view> choices) {
 for(auto candidate:choices) if(value==candidate)return true;
 return false;
}
bool number(std::string_view value,float& out) {
 if(value.empty())return false;
 auto result=std::from_chars(value.data(),value.data()+value.size(),out);
 return result.ec==std::errc{} && result.ptr==value.data()+value.size() && std::isfinite(out);
}
bool length(std::string_view value,bool positive=false) {
 bool unit=false;
 if(value.ends_with("px")){value.remove_suffix(2);unit=true;}
 else if(value.ends_with("%")){value.remove_suffix(1);unit=true;}
 float n{};return number(value,n) && (unit || n==0) && (!positive || n>0);
}
bool admitted_value(std::string_view name,std::string_view value) {
 if(name=="font-family")return one_of(value,{"SyntheticTest","Arial"});
 if(name=="display")return one_of(value,{"block","flex","inline","inline-block","none","table"});
 if(name=="white-space")return one_of(value,{"nowrap","normal","pre","pre-wrap","pre-line"});
 if(name=="direction")return one_of(value,{"ltr","rtl"});
 if(name=="writing-mode")return one_of(value,{"horizontal-tb","vertical-rl","vertical-lr"});
 if(name=="text-transform")return one_of(value,{"none","uppercase","lowercase","capitalize"});
 if(name=="text-align")return one_of(value,{"start","left","center","end","right","justify"});
 if(name=="position")return one_of(value,{"static","relative","absolute","fixed"});
 if(name=="flex-direction")return one_of(value,{"row","row-reverse","column","column-reverse"});
 if(name=="align-items")return one_of(value,{"stretch","flex-start","flex-end","center","baseline"});
 if(name=="justify-content")return one_of(value,{"flex-start","flex-end","center","space-between","space-around","space-evenly"});
 if(name=="font-weight")return one_of(value,{"100","200","300","400","500","600","700","800","900"});
 if(name=="color" || name=="background-color")return one_of(value,{"black","white","red","blue","green","transparent"});
 if(name=="font-size")return length(value,true);
 if(name=="line-height") {float n{};return value=="normal" || (number(value,n)&&n>0) || length(value,true);}
 if(name=="opacity" || name=="flex-grow" || name=="flex-shrink") {
  float n{};return number(value,n)&&n>=0 && (name!="opacity" || n<=1);
 }
 if(name=="padding")return length(value) && !value.starts_with('-');
 if(name=="margin")return value=="auto" || length(value);
 return value=="auto" || ((name=="width" || name=="min-width" || name=="max-width") &&
   one_of(value,{"min-content","max-content","fit-content"})) ||
   (length(value) && !value.starts_with('-'));
}
bool admitted_style(std::string_view source) {
 source=trim(source);
 while(!source.empty()) {
  auto stop=source.find(';');
  auto declaration=trim(source.substr(0,stop));
  if(declaration.empty()) return false;
  auto colon=declaration.find(':');
  if(colon==std::string_view::npos) return false;
  auto name=trim(declaration.substr(0,colon)), value=trim(declaration.substr(colon+1));
  if(!known_property(name) || value.empty()) return false;
  for(unsigned char c:value) if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') ||
      (c>='0'&&c<='9') || c=='-' || c=='.' || c=='%' || c=='#' || c==' ')) return false;
  if(!admitted_value(name,value))return false;
  if(stop==std::string_view::npos) break;
  source=trim(source.substr(stop+1));
 }
 return true;
}
bool fixture_dom(aether_doc_s* doc) {
 std::vector<aether_node_s*> stack{doc};size_t nodes=0,bytes=0;
 while(!stack.empty()) {
  auto* n=stack.back();stack.pop_back();
  if(++nodes>65536 || !n || !n->identity->alive || !n->identity->attached || n->is_generated) return false;
  std::string tag=n->tag_name;
  for(char& c:tag) if(c>='A' && c<='Z') c=char(c-'A'+'a');
  if(tag=="style" || tag=="link") return false;
  for(const auto& [key,value]:n->attributes) {
   if(value.size()>1024*1024-bytes) return false;
   bytes+=value.size();
   std::string attr=key;
   for(char& c:attr) if(c>='A' && c<='Z') c=char(c-'A'+'a');
   if(attr=="style" && !admitted_style(value)) return false;
  }
  for(auto* child:n->children) stack.push_back(child);
 }
 return true;
}
}
bool SyntheticTextTestIssuer::create_fixture_styles(aether_doc_s* doc,
 const aether_style_environment_t& environment, aether_style_runtime_t* out_runtime,
 aether_style_map_t* out_map, std::shared_ptr<const SyntheticFixtureAuthority>& out_authority) {
 if(!doc || !out_runtime || !out_map) return false;
 const CallbackEpochGuard guard(doc->identity);
 if(!fixture_dom(doc) || !guard.valid()) return false;
 aether_style_runtime_t runtime{}; aether_style_transaction_t transaction{};
 aether_style_revision_t revision{}; aether_style_map_t map{};
 struct Cleanup {aether_style_runtime_t& r;aether_style_transaction_t& t;aether_style_revision_t& v;aether_style_map_t& m;
  ~Cleanup(){if(m)aether_style_map_destroy(m);if(v)aether_style_revision_destroy(v);if(t)aether_style_transaction_destroy(t);if(r)aether_style_runtime_destroy(r);}
 } cleanup{runtime,transaction,revision,map};
 // A new default runtime: no caller-supplied registry, sheet, resolver or hooks.
 if(aether_style_runtime_create(nullptr,&environment,nullptr,nullptr,&runtime)!=AETHER_OK ||
    aether_style_runtime_prepare_revision(runtime,doc,&environment,&transaction)!=AETHER_OK ||
    !guard.valid() ||
    aether_style_runtime_commit_revision(runtime,transaction,&revision)!=AETHER_OK ||
    aether_style_revision_create_map(revision,&map)!=AETHER_OK) return false;
 auto version=aether::style::pin_style_map(map);
 if(!version || !version->contextual() || version->document_lifetime()!=doc->identity ||
    !aether::style::inputs_current(version.get())) return false;
 auto authority=std::shared_ptr<const SyntheticFixtureAuthority>(new SyntheticFixtureAuthority(version,doc->identity));
 *out_runtime=std::exchange(runtime,nullptr);*out_map=std::exchange(map,nullptr);
 out_authority=std::move(authority);return true;
}
bool SyntheticTextTestIssuer::install(aether_layout_engine_s& engine, aether_doc_s* doc,
 aether_style_map_s* map, std::shared_ptr<const SyntheticFixtureAuthority> authority,
 const std::vector<aether_node_s*>& owners,
 std::shared_ptr<const aether::rendering::text::SyntheticFaceSeal> seal,
 std::shared_ptr<const aether::rendering::text::ContextualMeasurementProvider> provider,
 std::shared_ptr<const std::atomic_bool> cancelled) {
 if(engine.layout_in_progress || !doc || !map || !authority || !seal || !provider) return false;
 auto version=aether::style::pin_style_map(map);
 if(!version || !version->contextual() || version->document_lifetime()!=doc->identity ||
    authority->version_!=version || authority->document_!=doc->identity || !authority->epochs_.valid()) return false;
 std::vector<FrozenNodeRef> records;
 for(auto* owner:owners) {
  auto record=version->backing()->find_owner(owner);
  if(!record || record->document_lifetime()!=doc->identity) return false;
  records.push_back(std::move(record));
 }
 auto session=std::make_shared<const int>(0);
 auto policy=std::shared_ptr<const SyntheticTextPolicy>(new SyntheticTextPolicy(&engine,std::move(authority),session,doc->identity,
   version,std::move(records),std::move(seal),std::move(provider),std::move(cancelled)));
 engine.synthetic_session_=std::move(session);engine.synthetic_text_policy_=std::move(policy);
 return true;
}
}
