#include "session.h"
#include "admission.h"
#include "measure_bridge.h"
#include "paint_finalize.h"
#include "raster_coordinate.h"
#include "../../layout/layout_internal.h"
#include "../preparation/native_command_source.h"
#include <map>
#include <set>
#include <optional>
#include <thread>
namespace aether::rendering::html_ltr {
namespace {
void require(bool v){if(!v)throw Error{AETHER_ERROR_INTERNAL};}
void core(aether_status_t s){if(s!=AETHER_OK)throw Error{s==AETHER_ERROR_OUT_OF_MEMORY?s:AETHER_ERROR_INTERNAL};}
constexpr const char* ua=
 "html,body,div,h1,p {display:block;position:static;box-sizing:content-box;width:auto;height:auto;margin-top:0px;margin-right:0px;margin-bottom:0px;margin-left:0px;padding-top:0px;padding-right:0px;padding-bottom:0px;padding-left:0px;border-width:0px;border-style:none;overflow:visible;opacity:1;background-color:transparent;}"
 "html {overflow-x:clip;overflow-y:clip;font-family:AetherLTR;font-size:16px;font-weight:400;line-height:24px;color:#202020;white-space:nowrap;text-align:start;direction:ltr;writing-mode:horizontal-tb;}";
aether_color_rgba color(unsigned rgb){return {float((rgb>>16)&255)/255,float((rgb>>8)&255)/255,float(rgb&255)/255,1};}
bool same(aether_color_rgba a,aether_color_rgba b){return a.r==b.r&&a.g==b.g&&a.b==b.b&&a.a==b.a;}
unsigned number(const std::string& s){unsigned n=0;for(char c:s){if(c<'0'||c>'9')break;n=n*10+unsigned(c-'0');}return n;}
unsigned rgb(const std::string& s){unsigned n=0;for(size_t i=1;i<s.size();++i){char c=s[i];n=n*16+unsigned(c<='9'?c-'0':(c|32)-'a'+10);}return n;}
Inventory admit_startup(std::string_view html,size_t font_bytes){
 if(font_bytes>16u*1024u*1024u||html.size()>65536)throw Error{AETHER_ERROR_LIMIT_EXCEEDED};
 // Reserve a conservative 1 MiB inventory/session allowance before recognition.
 // Together with copied inputs this is below the 32 MiB logical session quota;
 // FontProvider receives the same reservation before any subsequent H work.
 if(font_bytes+html.size()+1024u*1024u>32u*1024u*1024u)throw Error{AETHER_ERROR_LIMIT_EXCEEDED};
 HTML_LTR_STAGE(inventory);
 return admit(html);
}
bool trivia_same(std::string_view authored,std::string_view parsed) {
 // The HTML parser normalizes CRLF and bare CR to LF. Trivia has no visible
 // semantics, but its position and normalized whitespace must still match.
 size_t j=0;
 for(size_t i=0;i<authored.size();++i){char c=authored[i];
  if(c!=' '&&c!='\t'&&c!='\r'&&c!='\n')return false;
  if(c=='\r'){if(i+1<authored.size()&&authored[i+1]=='\n')++i;c='\n';}
  if(j==parsed.size()||parsed[j++]!=c)return false;
 }
 return j==parsed.size();
}
struct Flat {aether_computed_style_t s{};~Flat(){aether_computed_style_free(&s);}};
void verify_style(aether_style_map_t map,aether_node_t node,const Node& authored){
 auto handle=aether_style_map_get(map,node);require(handle);Flat flat;auto code=aether_computed_style_copy(handle,&flat.s);aether_computed_style_handle_destroy(handle);core(code);const auto& s=flat.s;
 const auto& e=authored.inherited;
 require(s.font_size==e.font_size&&s.font_weight==int(e.font_weight)&&s.line_height==e.line_height&&same(s.color,color(e.color_rgb))&&std::string_view(s.font_family)=="AetherLTR");
 require(s.display==AETHER_DISPLAY_BLOCK&&s.position==AETHER_POSITION_STATIC&&s.box_sizing==AETHER_BOX_SIZING_CONTENT_BOX&&s.height.unit==AETHER_CSS_UNIT_AUTO&&s.opacity==1&&s.visible&&s.white_space==AETHER_WHITE_SPACE_NOWRAP&&s.text_align==AETHER_TEXT_ALIGN_START&&s.text_transform==AETHER_TEXT_TRANSFORM_NONE&&s.overflow_x==(authored.tag=="html"?AETHER_OVERFLOW_CLIP:AETHER_OVERFLOW_VISIBLE)&&s.overflow_y==(authored.tag=="html"?AETHER_OVERFLOW_CLIP:AETHER_OVERFLOW_VISIBLE));
 require(!s.has_text_gradient&&!s.background_layers.count&&!s.backdrop_filters.count&&!s.transforms.count&&!s.text_shadows.count&&!s.box_shadows.count&&!s.transition_properties&&!s.transition_duration&&!s.transition_delay&&s.z_index_kind==AETHER_Z_INDEX_AUTO);
 const auto zero_radius=[](aether_corner_radius r){return r.horizontal.value==0&&r.vertical.value==0;};
 require(zero_radius(s.border_radii.top_left)&&zero_radius(s.border_radii.top_right)&&zero_radius(s.border_radii.bottom_left)&&zero_radius(s.border_radii.bottom_right));
 std::map<std::string,std::string> ds(authored.declarations.begin(),authored.declarations.end());
 const auto length=[&](const std::string& key,aether_css_length actual,bool aut=false){auto it=ds.find(key);if((it==ds.end()&&aut)||(it!=ds.end()&&it->second=="auto")){require(actual.unit==AETHER_CSS_UNIT_AUTO);return;}require(actual.unit==AETHER_CSS_UNIT_PX&&actual.value==(it==ds.end()?0:float(number(it->second))));};
 length("width",s.width,true);
 const auto edges=[&](const char* prefix,aether_css_edges x){length(std::string(prefix)+"-top",x.top);length(std::string(prefix)+"-right",x.right);length(std::string(prefix)+"-bottom",x.bottom);length(std::string(prefix)+"-left",x.left);};
 edges("margin",s.margin);edges("padding",s.padding);
 for(auto x:{s.border_width.top,s.border_width.right,s.border_width.bottom,s.border_width.left})length("border-width",x);
 auto bg=ds.find("background-color");require(same(s.background_color,bg==ds.end()||bg->second=="transparent"?aether_color_rgba{0,0,0,0}:color(rgb(bg->second))));
 auto bs=ds.find("border-style");const int border=bs!=ds.end()&&bs->second=="solid"?AETHER_BORDER_STYLE_SOLID:AETHER_BORDER_STYLE_NONE;
 require(s.border_styles.top==border&&s.border_styles.right==border&&s.border_styles.bottom==border&&s.border_styles.left==border);
 if(auto bc=ds.find("border-color");bc!=ds.end()){auto col=color(rgb(bc->second));require(same(s.border_colors.top,col)&&same(s.border_colors.right,col)&&same(s.border_colors.bottom,col)&&same(s.border_colors.left,col));}
}
}
struct HtmlLtrSession::State {
 struct Owner {size_t ordinal; aether::style::FrozenNodeRef record;std::shared_ptr<const FontProduct> whole;};
 struct Occurrence {std::shared_ptr<const text::ShapedRun> run;std::shared_ptr<const FontProduct> product;aether_node_s* owner;uint64_t id;};
 std::string html;Inventory inventory;FontProvider fonts;Viewport viewport;
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_revision_t revision{};aether_style_map_t map{};aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};
 std::vector<aether_node_t> nodes;std::map<aether_node_t,Owner> owners;std::map<const text::ShapedRun*,Occurrence> occurrences;std::unique_ptr<NativePaintList> paint;
 std::vector<std::shared_ptr<const text::GlyphMaskA8>> masks;std::optional<preparation::PreparedFrame> prepared;
 uint64_t epoch{},nonstate{},serial{};std::thread::id thread=std::this_thread::get_id();aether_status_t bridge_error=AETHER_OK;
 State(std::string h,std::vector<uint8_t> r,std::vector<uint8_t> b,Viewport v):html(std::move(h)),inventory(admit_startup(html,r.size()+b.size())),fonts(std::move(r),std::move(b)),viewport(v){fonts.reserve_payload(html.size()+1024*1024);}
 ~State(){paint.reset();aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_revision_destroy(revision);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 void valid()const {require(thread==std::this_thread::get_id()&&doc&&doc->identity->alive&&doc->identity->structure_epoch==epoch&&doc->identity->non_state_epoch==nonstate&&aether_style_map_structure_valid(map)&&aether_style_map_inputs_current(map));}
 struct Bridge final:HtmlLtrMeasureBridge {State* s;explicit Bridge(State* p):s(p){}
  text::Measurement measure(const char* raw,const aether::layout::BoxStyleSnapshot& st,float width,aether_node_s* node,bool transport)override {
   try{return s->measure(raw,st,width,node,transport);}catch(const Error& e){s->bridge_error=e.status;throw;}catch(const BoundaryError& e){s->bridge_error=e.status;throw;}
  }
 };
 text::Measurement measure(const char* raw,const aether::layout::BoxStyleSnapshot& st,float width,aether_node_s* node,bool transport){
  valid();require(raw&&std::isfinite(width)&&width==0&&owners.contains(node));const auto& owner=owners.at(node);const auto& authored=inventory.nodes[owner.ordinal];
  require(node->identity==owner.record->node_lifetime()&&node->identity->alive&&node->identity->attached&&node->identity->owner==node&&st.style_owner()==owner.record->style());
  const auto& exp=authored.inherited;require(st.font_size==exp.font_size&&st.font_weight==int(exp.font_weight)&&st.line_height==exp.line_height&&same(st.color,color(exp.color_rgb)));
  HTML_LTR_STAGE(occurrence);
  auto product=std::make_shared<const FontProduct>(fonts.product(raw,int(exp.font_size),int(exp.font_weight),int(exp.line_height)));
  if(!transport)return {product->metrics,{}};
  require(*raw);fonts.reserve_payload(sizeof(Occurrence)+sizeof(text::ShapedRun)+product->glyphs.size()*sizeof(text::Glyph)+std::strlen(raw));
  validate_run_transport(raw,product->glyphs,product->metrics.width,product->metrics.height,product->metrics.baseline);
  auto run=std::make_shared<const text::ShapedRun>(product->binding->font(),raw,product->glyphs,product->metrics);
  occurrences.emplace(run.get(),Occurrence{run,product,node,++serial});return {product->metrics,run};
 }
 void verify_dom(){
  nodes.resize(inventory.nodes.size());std::vector<std::pair<aether_node_t,size_t>> pending{{aether_doc_get_document_element(doc),0}};
  while(!pending.empty()){auto [node,idx]=pending.back();pending.pop_back();const auto& authored=inventory.nodes[idx];require(node&&node->type==AETHER_NODE_ELEMENT&&node->tag_name==authored.tag&&!node->is_generated);nodes[idx]=node;
   if(authored.leaf()){require(node->children.size()==(authored.text.empty()?0u:1u));if(!authored.text.empty())require(node->children[0]->type==AETHER_NODE_TEXT&&node->children[0]->text==authored.text);continue;}
   size_t child=0,trivia=0;
   for(auto* n:node->children){if(n->type==AETHER_NODE_TEXT){require(trivia<authored.trivia.size()&&authored.trivia[trivia].before_child==child&&trivia_same(authored.trivia[trivia].text,n->text));++trivia;}else{require(n->type==AETHER_NODE_ELEMENT&&child<authored.children.size());pending.push_back({n,authored.children[child++]});}}
   require(child==authored.children.size()&&trivia==authored.trivia.size());
  }
  for(auto n:nodes)require(n);
  require(aether_doc_get_body(doc)==nodes[1]);
 }
 void run(){
  require(!prepared);core(aether_doc_create(&doc));core(aether_doc_parse_html(doc,html.c_str()));verify_dom();
  HTML_LTR_STAGE(runtime);
  aether_style_runtime_options_t options{};aether_style_environment_t env{float(viewport.width),float(viewport.height),1,16,0,0,0};core(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime));int64_t id{};core(aether_style_runtime_add_stylesheet(runtime,ua,0,"private-html-ltr",&id,nullptr));
  aether_style_transaction_t tx{};core(aether_style_runtime_prepare_revision(runtime,nodes[0],&env,&tx));const auto committed=aether_style_runtime_commit_revision(runtime,tx,&revision);aether_style_transaction_destroy(tx);core(committed);core(aether_style_revision_create_map(revision,&map));
  epoch=doc->identity->structure_epoch;nonstate=doc->identity->non_state_epoch;
  for(size_t i=0;i<nodes.size();++i){verify_style(map,nodes[i],inventory.nodes[i]);if(inventory.nodes[i].leaf()){auto& e=inventory.nodes[i];auto whole=std::make_shared<const FontProduct>(fonts.product(e.text,int(e.inherited.font_size),int(e.inherited.font_weight),int(e.inherited.line_height)));auto record=map->current->backing()->find_owner(nodes[i]);require(bool(record));owners.emplace(nodes[i],Owner{i,record,whole});}}
  core(aether_layout_create(nullptr,nullptr,&engine));HtmlLtrSession::install(engine,std::make_shared<Bridge>(this));
  HTML_LTR_STAGE(layout);
  auto status=aether_layout_layout(engine,doc,map,float(viewport.width),float(viewport.height),nullptr,nullptr,&layout);
  if(status!=AETHER_OK){if(bridge_error!=AETHER_OK)throw Error{bridge_error};core(status);}valid();
  #ifdef AETHER_HTML_LTR_TESTING
  testing::inspect(layout,nullptr);
#endif
  FinalTexts final;std::map<aether_node_t,std::string> collected;std::map<aether_node_t,float> baselines;std::vector<const aether_layout_box_s*> todo{layout->root};size_t placed=0;
  while(!todo.empty()){auto* box=todo.back();todo.pop_back();require(box);auto r=box->content_box;for(double d:{double(r.x),double(r.y),double(r.width),double(r.height)})if(!std::isfinite(d))throw Error{AETHER_ERROR_INTERNAL};
   if(box->kind==LAYOUT_BOX_TEXT){auto run=box->native_text.run;require(run&&occurrences.contains(run.get()));const auto& occurrence=occurrences.at(run.get());require(occurrence.owner==box->element&&run->source==box->text&&run->metrics.width==r.width&&run->metrics.height==r.height&&run->metrics.baseline==box->native_text.baseline);require(owners.at(box->element).record->style()==box->style.style_owner());
    const float baseline=r.y+run->metrics.baseline;if(baselines.contains(box->element))require(baselines.at(box->element)==baseline);else baselines.emplace(box->element,baseline);
    reserve_count(placed,run->glyphs.size(),16384);collected[box->element]+=box->text;require(final.emplace(run.get(),FinalText{run,occurrence.product,r}).second);
   }
   for(auto it=box->children.rbegin();it!=box->children.rend();++it)todo.push_back(*it);
  }
  for(size_t i=1;i<nodes.size();++i)require(aether_layout_result_find_primary_box(layout,nodes[i]));
  for(const auto& [node,owner]:owners)require(collected[node]==inventory.nodes[owner.ordinal].text);
  core(aether_display_list_builder_create(nullptr,nullptr,&builder));core(build_native_opacity(builder,layout,map,nullptr,paint));valid();
  #ifdef AETHER_HTML_LTR_TESTING
  testing::inspect(layout,paint.get());
#endif
  masks=finalize_paint(*paint,final,fonts);valid();HTML_LTR_STAGE(snapshot);auto snapshot=preparation::snapshot_native_opacity(*paint);
  if(auto e=std::get_if<preparation::Error>(&snapshot))throw Error{e->code==preparation::ErrorCode::OUT_OF_MEMORY?AETHER_ERROR_OUT_OF_MEMORY:e->code==preparation::ErrorCode::LIMIT_EXCEEDED?AETHER_ERROR_LIMIT_EXCEEDED:AETHER_ERROR_INTERNAL};
  HTML_LTR_STAGE(preparation);auto frame=preparation::prepare(std::get<preparation::Snapshot>(snapshot));if(auto e=std::get_if<preparation::Error>(&frame))throw Error{e->code==preparation::ErrorCode::OUT_OF_MEMORY?AETHER_ERROR_OUT_OF_MEMORY:e->code==preparation::ErrorCode::LIMIT_EXCEEDED?AETHER_ERROR_LIMIT_EXCEEDED:AETHER_ERROR_INTERNAL};valid();prepared.emplace(std::get<preparation::PreparedFrame>(std::move(frame)));
 }
};
void HtmlLtrSession::install(aether_layout_engine_s* e,std::shared_ptr<HtmlLtrMeasureBridge> b){require(e&&!e->layout_in_progress&&!e->html_ltr_bridge_&&!e->text_selection_&&!e->synthetic_text_policy_&&!e->text_measurer&&!e->contextual_text_measurer);e->html_ltr_bridge_=std::move(b);}
HtmlLtrSession::HtmlLtrSession(std::string h,std::vector<uint8_t> r,std::vector<uint8_t> b,Viewport v):state_(std::make_unique<State>(std::move(h),std::move(r),std::move(b),v)){}
HtmlLtrSession::~HtmlLtrSession()=default;
const preparation::PreparedFrame& HtmlLtrSession::prepare(){state_->run();return *state_->prepared;}
const std::vector<std::shared_ptr<const text::GlyphMaskA8>>& HtmlLtrSession::masks()const noexcept{return state_->masks;}
}
