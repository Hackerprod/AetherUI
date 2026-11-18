#include "b0_source_session.h"
#include "../../style_runtime/dom_stylesheet.h"
#include "../../css/style_c_export.h"
#include "session_validation.h"
#include "admission.h"
#include "measure_bridge.h"
#include "paint_finalize.h"
#include "raster_coordinate.h"
#include "control_presentation.h"
#include "b2_session_provenance.h"
#include "../../layout/inline_text.h"
#include "../../layout/form_controls.h"
#include "../../layout/layout_internal.h"
#include "../preparation/native_command_source.h"
#include <map>
#include <set>
#include <optional>
#include <thread>
namespace aether::rendering::html_ltr {
using namespace session_validation;

struct B0SourceSession::State {
 struct Owner {size_t ordinal; aether::style::FrozenNodeRef record;std::shared_ptr<const FontProduct> whole;std::shared_ptr<const ControlPresentation> control;};
 struct Occurrence {std::shared_ptr<const text::ShapedRun> run;std::shared_ptr<const FontProduct> product;aether_node_s* owner;uint64_t id;};
 bool b2_enabled=false;std::unique_ptr<B2Provenance> provenance;
 uint32_t profile=2;std::string html;FontProvider fonts;Viewport viewport;
 uint32_t stage=AETHER_B0_STAGE_INPUT;uint64_t diagnostic_node=AETHER_B0_NO_INDEX;
 std::map<aether_node_t,std::string> visible;std::vector<aether_node_t> styles;
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_revision_t revision{};aether_style_map_t map{};aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};
 std::vector<aether_node_t> nodes;std::map<aether_node_t,Owner> owners;std::map<const text::ShapedRun*,Occurrence> occurrences;std::unique_ptr<NativePaintList> paint;
 std::vector<std::shared_ptr<const text::GlyphMaskA8>> masks;std::optional<preparation::PreparedFrame> prepared;
 uint64_t epoch{},nonstate{},serial{};std::thread::id thread=std::this_thread::get_id();aether_status_t bridge_error=AETHER_OK;
 State(std::string h,std::vector<uint8_t> r,std::vector<uint8_t> b,Viewport v):html(std::move(h)),fonts(std::move(r),std::move(b),2),viewport(v){fonts.reserve_payload(html.size()+1024*1024);if(!fonts.has_bold())throw Error{AETHER_ERROR_INVALID_ARGUMENT};}

 ~State(){provenance.reset();prepared.reset();masks.clear();paint.reset();aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_revision_destroy(revision);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 void valid()const {require(thread==std::this_thread::get_id()&&doc&&doc->identity->alive&&doc->identity->structure_epoch==epoch&&doc->identity->non_state_epoch==nonstate&&aether_style_map_structure_valid(map)&&aether_style_map_inputs_current(map));}
 template<class F> decltype(auto) guarded(F&& f){try{return f();}catch(const Error& e){bridge_error=e.status;throw;}catch(const BoundaryError& e){bridge_error=e.status;throw;}catch(const aether::layout::b2::LimitExceeded&){bridge_error=AETHER_ERROR_LIMIT_EXCEEDED;throw;}catch(const aether::layout::b2::InvalidLedger&){bridge_error=AETHER_ERROR_INTERNAL;throw;}}
 struct Bridge final:HtmlLtrMeasureBridge {State* s;explicit Bridge(State* p):s(p){}
  bool b2_inline_policy()const noexcept override{return s->b2_enabled;}
  bool b2_inline_owner(aether_node_s* n)const noexcept override{return s->b2_enabled&&n&&!State::control(n);}
  std::pmr::memory_resource* b2_resource()override{return s->guarded([&](){s->valid();require(bool(s->provenance));return s->provenance->resource();});}
  void b2_work(B2Work w,size_t n)override{return s->guarded([&](){s->valid();s->provenance->work(w,n);s->valid();});}
  bool b2_begin_collection(const B2InlineResult& r)override{return s->guarded([&](){s->valid();const auto first=s->provenance->begin_collection(r);s->valid();return first;});}
  void b2_preflight(const B2InlineResult& r,size_t t,size_t v)override{return s->guarded([&](){s->valid();s->provenance->preflight(r,t,v);s->valid();});}
  void b2_inline_result(const B2InlineResult& r)override{return s->guarded([&](){s->valid();s->provenance->inline_result(r);s->valid();});}
  std::shared_ptr<const ControlPresentation> control(aether_node_s* node,const aether::layout::BoxStyleSnapshot& st) override {
   s->valid();require(s->profile==2&&s->owners.contains(node));const auto& o=s->owners.at(node);require(o.control&&st.style_owner()==o.record->style());s->check_control(o.control,node,st);return o.control;
  }
  uint64_t occurrence(const text::ShapedRun* run) const override {s->valid();require(s->occurrences.contains(run));return s->occurrences.at(run).id;}

  text::Measurement measure(const char* raw,const aether::layout::BoxStyleSnapshot& st,float width,aether_node_s* node,bool transport)override {
   try{return s->measure(raw,st,width,node,transport);}catch(const Error& e){s->bridge_error=e.status;throw;}catch(const BoundaryError& e){s->bridge_error=e.status;throw;}catch(const aether::layout::b2::LimitExceeded&){s->bridge_error=AETHER_ERROR_LIMIT_EXCEEDED;throw;}catch(const aether::layout::b2::InvalidLedger&){s->bridge_error=AETHER_ERROR_INTERNAL;throw;}
  }
 };
 text::Measurement measure(const char* raw,const aether::layout::BoxStyleSnapshot& st,float width,aether_node_s* node,bool transport){
  valid();if(provenance)provenance->counters().charge(aether::layout::b2::Quota::bridge_calls);require(raw&&std::isfinite(width)&&width==0&&owners.contains(node));const auto& owner=owners.at(node);
  require(node->identity==owner.record->node_lifetime()&&node->identity->alive&&node->identity->attached&&node->identity->owner==node&&st.style_owner()==owner.record->style());
  if(owner.control){check_control(owner.control,node,st);require(std::string_view(raw)==owner.control->text);}else require(b2_enabled||visible.contains(node));
  aether::layout::BoxStyleSnapshot exp(owner.record->style(),owner.record->measures());require(st.font_size==exp.font_size&&st.font_weight==exp.font_weight&&st.line_height==exp.line_height&&same(st.color,exp.color));
  HTML_LTR_STAGE(occurrence);
  auto product=std::make_shared<const FontProduct>(fonts.product(raw,int(exp.font_size),exp.font_weight,int(exp.line_height)));
  if(!transport)return {product->metrics,{}};
  require(*raw);fonts.reserve_payload(sizeof(Occurrence)+sizeof(text::ShapedRun)+product->glyphs.size()*sizeof(text::Glyph)+std::strlen(raw));
  validate_run_transport(raw,product->glyphs,product->metrics.width,product->metrics.height,product->metrics.baseline);
  auto run=std::make_shared<const text::ShapedRun>(product->binding->font(),raw,product->glyphs,product->metrics);
  occurrences.emplace(run.get(),Occurrence{run,product,node,++serial});if(provenance&&!control(node))provenance->record_measurement(run,node,st.style_owner().get());valid();return {product->metrics,run};
 }
 void check_control(const std::shared_ptr<const ControlPresentation>& c,aether_node_s* node,const aether::layout::BoxStyleSnapshot& st){
  valid();require(profile==2&&c&&c->profile==profile&&c->epoch==epoch&&c->nonstate==nonstate&&c->ordinal<nodes.size()&&nodes[c->ordinal]==node&&owners.contains(node));
  const auto& o=owners.at(node);require(o.control==c&&o.record==c->record&&c->record->style()==st.style_owner()&&node->identity==c->record->node_lifetime()&&node->identity->alive&&node->identity->attached&&node->identity->owner==node);
  require(o.whole&&c->text==o.whole->source);
  aether::layout::BoxStyleSnapshot expected(o.record->style(),o.record->measures());
  require(st.font_size==expected.font_size&&st.font_weight==expected.font_weight&&st.line_height==expected.line_height&&same(st.color,expected.color));
  require(node->tag_name=="input"&&attribute(node,"type")==c->type&&c->shows_placeholder==attribute(node,"value").empty());
  const auto kind=c->text.empty()?ControlKind::empty:(c->shows_placeholder?ControlKind::placeholder:ControlKind::value);require(c->kind==kind);
  require(same(c->color,c->shows_placeholder?aether_color_rgba{0.42f,0.45f,0.5f,1.f}:expected.color));
 }

 // DOM and style validators below.
 static constexpr const char* b0_ua=
 "html,head,title,style,meta,body,div,main,section,aside,h1,h2,p,form,label,input,button {display:block;position:static;box-sizing:content-box;width:auto;height:auto;margin-top:0px;margin-right:0px;margin-bottom:0px;margin-left:0px;padding-top:0px;padding-right:0px;padding-bottom:0px;padding-left:0px;border-width:0px;border-style:none;overflow:visible;opacity:1;background-color:transparent;}"
 "html {overflow-x:clip;overflow-y:clip;font-family:AetherB0;font-size:16px;font-weight:400;line-height:24px;color:#202020;white-space:nowrap;text-align:start;direction:ltr;writing-mode:horizontal-tb;}"
 "head,title,style,meta {display:none;}"
 "span,strong,a,br {position:static;box-sizing:content-box;width:auto;height:auto;margin-top:0px;margin-right:0px;margin-bottom:0px;margin-left:0px;padding-top:0px;padding-right:0px;padding-bottom:0px;padding-left:0px;border-width:0px;border-style:none;overflow:visible;opacity:1;background-color:transparent;}"
 "span,strong,a {display:inline;}strong {font-weight:700;}input,button{white-space:nowrap;text-align:start;}";
 [[noreturn]] void fail(uint32_t reason=AETHER_B0_REASON_UNSUPPORTED,aether_status_t status=AETHER_ERROR_UNSUPPORTED)const{throw B0SourceError{status,stage,reason,diagnostic_node};}
 static const std::string& attribute(aether_node_t node,const char* name){static const std::string empty;auto i=node->attributes.find(name);return i==node->attributes.end()?empty:i->second;}
 static bool metadata(aether_node_t node){return node->tag_name=="head"||node->tag_name=="title"||node->tag_name=="style"||node->tag_name=="meta";}
 static bool leaf(aether_node_t node){return node->tag_name=="h1"||node->tag_name=="h2"||node->tag_name=="p"||node->tag_name=="label"||node->tag_name=="button";}
 static bool container(aether_node_t node){const auto& t=node->tag_name;return t=="body"||t=="div"||t=="form"||t=="main"||t=="section"||t=="aside";}
 static bool control(aether_node_t node){return node->tag_name=="input"||node->tag_name=="button";}
 static bool inline_node(aether_node_t n){return n->tag_name=="span"||n->tag_name=="strong"||n->tag_name=="a";}
 static bool br(aether_node_t n){return n->tag_name=="br";}
 static bool paragraph(aether_node_t n){return leaf(n)&&!control(n);}
 static bool trivia(std::string_view s){for(char c:s)if(c!=' '&&c!='\t'&&c!='\r'&&c!='\n')return false;return true;}
 size_t scalars(std::string_view s,bool simple){size_t count=0;bool space=false;for(size_t i=0;i<s.size();){auto cp=decode_scalar_utf8(s,i);if(!repertoire_scalar(cp))fail();if(simple&&(cp==32&&(count==0||space||i==s.size())))fail();space=cp==32;++count;}return count;}
 void verify_dom(){
  auto* root=aether_doc_get_document_element(doc);if(!root||root->tag_name!="html"||!aether_doc_get_body(doc))fail();
  // Inspect decoded DOM once before deciding whether legacy text admission applies.
  std::vector<aether_node_t> scan{root};size_t dom_count=0,text_count=0,comment_count=0,bytes=0;
  while(!scan.empty()) {auto* n=scan.back();scan.pop_back();
   if(++dom_count>1024)fail(AETHER_B0_REASON_LIMIT,AETHER_ERROR_LIMIT_EXCEEDED);
   if(n->type==AETHER_NODE_TEXT||n->type==AETHER_NODE_COMMENT){
    if(n->text.size()>65536-bytes)fail(AETHER_B0_REASON_LIMIT,AETHER_ERROR_LIMIT_EXCEEDED);
    bytes+=n->text.size();
    if(n->type==AETHER_NODE_TEXT&&++text_count>512)fail(AETHER_B0_REASON_LIMIT,AETHER_ERROR_LIMIT_EXCEEDED);
    if(n->type==AETHER_NODE_COMMENT&&++comment_count>256)fail(AETHER_B0_REASON_LIMIT,AETHER_ERROR_LIMIT_EXCEEDED);
    if(n->type==AETHER_NODE_TEXT&&n->parent&&!metadata(n->parent)&&!control(n->parent)){
     if(container(n->parent)&&!trivia(n->text))b2_enabled=true;
     if(paragraph(n->parent)) {bool previous=false;for(size_t i=0;i<n->text.size();++i){const unsigned char c=n->text[i];if(c=='\v')b2_enabled=true;const bool ws=aether_b2_inline_space_byte(c);if(ws&&(c!=' '||i==0||i+1==n->text.size()||previous))b2_enabled=true;previous=ws;}}
    }
   }
   if(inline_node(n)||br(n))b2_enabled=true;
   for(auto* c:n->children)scan.push_back(c);
  }
  std::vector<std::pair<aether_node_t,size_t>> pending{{root,1}};size_t controls=0,total=0,heads=0,bodies=0,viewports=0;
  while(!pending.empty()){
   auto [node,depth]=pending.back();pending.pop_back();diagnostic_node=nodes.size();
   if(node->type!=AETHER_NODE_ELEMENT||node->is_generated)fail();
   if(depth>16||nodes.size()>=256)fail(AETHER_B0_REASON_LIMIT,AETHER_ERROR_LIMIT_EXCEEDED);
   const auto& t=node->tag_name;auto* parent=node->parent;
   if(t!="html"&&!metadata(node)&&!container(node)&&!leaf(node)&&!control(node)&&!inline_node(node)&&!br(node))fail();
   if(t=="html"&&node!=root)fail();
   if(t=="head"){if(parent!=root||++heads!=1)fail();}
   if(t=="body"){if(parent!=root||++bodies!=1)fail();}
   if(t=="meta"||t=="title"||t=="style"){if(!parent||parent->tag_name!="head")fail();}
   if(parent==root&&t!="head"&&t!="body")fail();
   if(parent&&parent->tag_name=="head"&&!metadata(node))fail();
   if((container(node)&&t!="body")||paragraph(node)||control(node))if(!parent||!container(parent))fail();
   if(inline_node(node)||br(node))if(!parent||!(container(parent)||paragraph(parent)||inline_node(parent)))fail();
   if(t=="a")for(auto* a=parent;a;a=a->parent)if(a->tag_name=="a")fail();
   for(const auto& [key,value]:node->attributes){
    bool ok=key=="id"||key=="class"||(!br(node)&&(key=="lang"||key=="style"));
    ok=ok||(t=="a"&&key=="href")||(t=="label"&&key=="for")||(t=="input"&&(key=="type"||key=="value"||key=="placeholder"))||(t=="button"&&key=="type")||(t=="meta"&&(key=="charset"||key=="name"||key=="content"));
    if(!ok)fail();
    if(t=="a"&&key=="href"){
     const auto alpha=[](unsigned char c){return(c>='A'&&c<='Z')||(c>='a'&&c<='z');};
     if(value.size()<2||value.size()>65||value[0]!='#'||!alpha(value[1]))fail();
     for(size_t k=2;k<value.size();++k){const unsigned char c=value[k];if(!alpha(c)&&!(c>='0'&&c<='9')&&c!='_'&&c!='-')fail();}
    }
   }
   if(t=="meta"){
    const bool charset=node->attributes.size()==1&&attribute(node,"charset")=="utf-8";
    const bool viewport=node->attributes.size()==2&&attribute(node,"name")=="viewport"&&attribute(node,"content")=="width=device-width, initial-scale=1";
    if(!charset&&!viewport)fail();
    if(viewport&&++viewports!=1)fail();
   }
   if(control(node)&&++controls>32)fail(AETHER_B0_REASON_LIMIT,AETHER_ERROR_LIMIT_EXCEEDED);
   if(t=="input"){
    if(attribute(node,"type")!="text"&&attribute(node,"type")!="password")fail();
    if(!node->children.empty())fail();
    size_t count=scalars(attribute(node,"value"),false),placeholder=scalars(attribute(node,"placeholder"),false);
    if(count>256||placeholder>256)fail(AETHER_B0_REASON_LIMIT,AETHER_ERROR_LIMIT_EXCEEDED);
    total+=count?count:placeholder;
   }
   if(br(node)&&!node->children.empty())fail();
   if(t=="button"&&attribute(node,"type")!="button")fail();
   std::string text;bool pure=leaf(node);
   for(auto it=node->children.rbegin();it!=node->children.rend();++it){auto* child=*it;
    if(child->type==AETHER_NODE_ELEMENT){pure=false;if(metadata(node)&&t!="head")fail();if(t=="button")fail();pending.emplace_back(child,depth+1);}
    else if(child->type==AETHER_NODE_TEXT){if(t=="html"||t=="head"||t=="meta")if(!trivia(child->text))fail();}
    else if(child->type!=AETHER_NODE_COMMENT)fail();
   }
   if(pure){for(auto* child:node->children)if(child->type==AETHER_NODE_TEXT)text+=child->text;
    if(t=="button"){const auto count=scalars(text,true);if(t=="button"&&count>256)fail(AETHER_B0_REASON_LIMIT,AETHER_ERROR_LIMIT_EXCEEDED);total+=count;}
    visible.emplace(node,std::move(text));
   }
   if(total>4096)fail(AETHER_B0_REASON_LIMIT,AETHER_ERROR_LIMIT_EXCEEDED);
   nodes.push_back(node);if(t=="style")styles.push_back(node);
  }
  if(bodies!=1)fail();
 }
 void verify_style(aether_node_t node){
  auto record=map->current->backing()->find_owner(node);require(bool(record));
  const auto& measures=record->measures();
  if(measures&&(!measures->contextual_lengths().empty()||!measures->intrinsic_keywords().empty()||measures->scalars().flex_order!=0||measures->scalars().direction_rtl||measures->scalars().writing_mode!=0))fail();
  Flat flat;core(aether::style::export_owned_flat(record->style_view(),&flat.s));const auto& s=flat.s;
  const auto supported=[&](bool v){if(!v)fail();};
  supported(std::string_view(s.font_family)=="AetherB0"&&std::isfinite(s.font_size)&&s.font_size>=8&&s.font_size<=48&&std::floor(s.font_size)==s.font_size&&(s.font_weight==400||s.font_weight==700)&&std::isfinite(s.line_height)&&s.line_height>=s.font_size&&s.line_height<=96&&std::floor(s.line_height)==s.line_height);
  supported(s.display==(metadata(node)?AETHER_DISPLAY_NONE:br(node)?AETHER_DISPLAY_LINE_BREAK:inline_node(node)?AETHER_DISPLAY_INLINE:AETHER_DISPLAY_BLOCK)&&s.position==AETHER_POSITION_STATIC&&s.box_sizing==AETHER_BOX_SIZING_CONTENT_BOX&&s.height.unit==AETHER_CSS_UNIT_AUTO&&s.opacity==1&&s.visible&&(s.white_space==AETHER_WHITE_SPACE_NOWRAP||(!control(node)&&s.white_space==AETHER_WHITE_SPACE_NORMAL))&&(s.text_align==AETHER_TEXT_ALIGN_START||(!control(node)&&s.text_align==AETHER_TEXT_ALIGN_CENTER))&&s.text_transform==AETHER_TEXT_TRANSFORM_NONE&&s.overflow_x==(node->tag_name=="html"?AETHER_OVERFLOW_CLIP:AETHER_OVERFLOW_VISIBLE)&&s.overflow_y==(node->tag_name=="html"?AETHER_OVERFLOW_CLIP:AETHER_OVERFLOW_VISIBLE));
  supported(!s.has_text_gradient&&!s.background_layers.count&&!s.backdrop_filters.count&&!s.transforms.count&&!s.text_shadows.count&&!s.box_shadows.count&&!s.transition_properties&&!s.transition_duration&&!s.transition_delay&&s.z_index_kind==AETHER_Z_INDEX_AUTO);
  const auto color_ok=[](aether_color_rgba c,bool transparent){return std::isfinite(c.r)&&std::isfinite(c.g)&&std::isfinite(c.b)&&c.r>=0&&c.r<=1&&c.g>=0&&c.g<=1&&c.b>=0&&c.b<=1&&(c.a==1||(transparent&&c.a==0));};
  supported(color_ok(s.color,false)&&color_ok(s.background_color,true));
  if(node->tag_name=="html")supported(s.background_color.a==0);
  const auto zero_radius=[](aether_corner_radius r){return r.horizontal.value==0&&r.vertical.value==0;};
  supported(zero_radius(s.border_radii.top_left)&&zero_radius(s.border_radii.top_right)&&zero_radius(s.border_radii.bottom_left)&&zero_radius(s.border_radii.bottom_right));
  const auto length=[&](aether_css_length x,bool aut=false,float max=8192){supported((aut&&x.unit==AETHER_CSS_UNIT_AUTO)||(x.unit==AETHER_CSS_UNIT_PX&&std::isfinite(x.value)&&x.value>=0&&x.value<=max));};
  length(s.width,true);
  for(auto x:{s.margin.top,s.margin.right,s.margin.bottom,s.margin.left,s.padding.top,s.padding.right,s.padding.bottom,s.padding.left})length(x);
  for(auto x:{s.border_width.top,s.border_width.right,s.border_width.bottom,s.border_width.left})length(x,false,128);
  supported(s.border_width.top.value==s.border_width.right.value&&s.border_width.top.value==s.border_width.bottom.value&&s.border_width.top.value==s.border_width.left.value);
  supported((s.border_styles.top==AETHER_BORDER_STYLE_NONE||s.border_styles.top==AETHER_BORDER_STYLE_SOLID)&&s.border_styles.top==s.border_styles.right&&s.border_styles.top==s.border_styles.bottom&&s.border_styles.top==s.border_styles.left);
  supported(color_ok(s.border_colors.top,false)&&same(s.border_colors.top,s.border_colors.right)&&same(s.border_colors.top,s.border_colors.bottom)&&same(s.border_colors.top,s.border_colors.left));
  if(s.white_space==AETHER_WHITE_SPACE_NORMAL||s.text_align==AETHER_TEXT_ALIGN_CENTER)b2_enabled=true;
  if(inline_node(node)||br(node)){
   supported(s.width.unit==AETHER_CSS_UNIT_AUTO&&s.background_color.a==0);
   for(auto x:{s.margin.top,s.margin.right,s.margin.bottom,s.margin.left,s.padding.top,s.padding.right,s.padding.bottom,s.padding.left,s.border_width.top,s.border_width.right,s.border_width.bottom,s.border_width.left})supported(x.value==0);
   supported(s.border_styles.top==AETHER_BORDER_STYLE_NONE);
   auto* block=node->parent;while(block&&inline_node(block))block=block->parent;require(block);
   auto block_record=map->current->backing()->find_owner(block);require(bool(block_record));aether::layout::BoxStyleSnapshot bs(block_record->style(),block_record->measures());
   supported(s.white_space==bs.white_space&&s.text_align==bs.text_align);
   if(br(node)){
    auto pr=map->current->backing()->find_owner(node->parent);require(bool(pr));aether::layout::BoxStyleSnapshot ps(pr->style(),pr->measures());
    supported(s.font_size==ps.font_size&&s.font_weight==ps.font_weight&&s.line_height==ps.line_height&&same(s.color,ps.color));
   }
  }

 }


 void run(){
  require(!prepared);stage=AETHER_B0_STAGE_DOM;core(aether_doc_create(&doc));core(aether_doc_parse_html(doc,html.c_str()));verify_dom();
  stage=AETHER_B0_STAGE_CSS;diagnostic_node=AETHER_B0_NO_INDEX;
  aether_style_runtime_options_t options{};aether_style_environment_t env{float(viewport.width),float(viewport.height),1,16,0,0,0};core(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime));int64_t id{};
  core(aether_style_runtime_add_stylesheet(runtime,b0_ua,0,"private-b0-source-v1",&id,nullptr));
  for(size_t i=0;i<styles.size();++i)core(aether::style::add_dom_stylesheet(runtime,doc,styles[i],i,&id));
  if(!aether::style::b0_source_stylesheets_accepted(runtime))fail(AETHER_B0_REASON_CSS_LOSS);
  aether_style_transaction_t tx{};core(aether_style_runtime_prepare_revision(runtime,nodes[0],&env,&tx));const auto committed=aether_style_runtime_commit_revision(runtime,tx,&revision);aether_style_transaction_destroy(tx);core(committed);core(aether_style_revision_create_map(revision,&map));
  epoch=doc->identity->structure_epoch;nonstate=doc->identity->non_state_epoch;
  for(size_t i=0;i<nodes.size();++i){auto record=map->current->backing()->find_owner(nodes[i]);require(bool(record));
   if(!record->typography()||!record->typography()->inputs||!aether::style::b0_source_accounting_accepted(*record->typography()->inputs))fail(AETHER_B0_REASON_CSS_LOSS);
  }
  stage=AETHER_B0_STAGE_STYLE;
  for(size_t i=0;i<nodes.size();++i){diagnostic_node=i;verify_style(nodes[i]);}
  if(!b2_enabled){
   stage=AETHER_B0_STAGE_DOM;size_t count=0;for(const auto& [n,t]:visible)count+=scalars(t,true);
   for(auto* n:nodes)if(n->tag_name=="input")count+=scalars(attribute(n,"value").empty()?attribute(n,"placeholder"):attribute(n,"value"),false);
   if(count>4096)fail(AETHER_B0_REASON_LIMIT,AETHER_ERROR_LIMIT_EXCEEDED);
  }
  stage=AETHER_B0_STAGE_TEXT;
  if(b2_enabled){
   for(auto* n:nodes)if(!metadata(n)&&!control(n))for(auto* c:n->children)if(c->type==AETHER_NODE_TEXT)if(!aether_b2_inline_text_supported(c->text))fail();
   provenance=std::make_unique<B2Provenance>(doc,map,fonts.regular_source(),fonts.bold_source());
  }
  core(aether_layout_create(nullptr,nullptr,&engine));B0SourceSession::install(engine,std::make_shared<Bridge>(this));
  if(provenance){
   size_t controls_visible=0;for(auto* n:nodes)if(control(n)){
    const auto& value=n->tag_name=="button"?visible.at(n):(attribute(n,"value").empty()?attribute(n,"placeholder"):attribute(n,"value"));
    for(size_t at=0;at<value.size();){(void)decode_scalar_utf8(value,at);++controls_visible;}
   }
   provenance->counters().charge(aether::layout::b2::Quota::visible_scalars,controls_visible);
   aether_b2_preflight_document(engine,doc,map);
  }
  for(size_t i=0;i<nodes.size();++i){diagnostic_node=i;auto* node=nodes[i];if(visible.contains(node)||node->tag_name=="input"||(b2_enabled&&!metadata(node))){
   auto record=map->current->backing()->find_owner(node);require(bool(record));std::shared_ptr<const ControlPresentation> presentation;
   aether::layout::BoxStyleSnapshot st(record->style(),record->measures());
   std::string shown=visible.contains(node)?visible.at(node):std::string();
   if(node->tag_name=="input"){
    fonts.reserve_payload(sizeof(ControlPresentation)+sizeof(AtomicControlPlacement)*4+sizeof(NativeControlEntry)*4+4096);
    shown=aether_opaque_control_display_text(node,st.scalars());
    const bool placeholder=attribute(node,"value").empty();
    if(placeholder)require(shown==attribute(node,"placeholder"));
    else if(attribute(node,"type")=="text")require(shown==attribute(node,"value"));
    else {
     // Validate masking before any FontProduct or ShapedRun can own the text.
     size_t count=0;const auto& value=attribute(node,"value");
     for(size_t at=0;at<value.size();){(void)decode_scalar_utf8(value,at);++count;}
     size_t bullets=0;for(size_t at=0;at<shown.size();){require(decode_scalar_utf8(shown,at)==0x2022);++bullets;}
     require(bullets==count);
    }
    auto c=std::make_shared<ControlPresentation>();c->profile=2;c->ordinal=i;c->record=record;c->epoch=epoch;c->nonstate=nonstate;c->type=attribute(node,"type");c->kind=shown.empty()?ControlKind::empty:(placeholder?ControlKind::placeholder:ControlKind::value);c->text=shown;c->shows_placeholder=placeholder;c->color=placeholder?aether_color_rgba{0.42f,0.45f,0.5f,1.f}:st.color;presentation=std::move(c);
   }
   std::shared_ptr<const FontProduct> whole;
   try {if(!b2_enabled||control(node))whole=std::make_shared<const FontProduct>(fonts.product(shown,int(st.font_size),st.font_weight,int(st.line_height)));}
   catch(const Error& e){if(e.status==AETHER_ERROR_UNSUPPORTED)fail(AETHER_B0_REASON_GLYPH_MISSING);throw;}
   owners.emplace(node,Owner{i,record,whole,presentation});
  }}
  stage=AETHER_B0_STAGE_LAYOUT;diagnostic_node=AETHER_B0_NO_INDEX;
  HTML_LTR_STAGE(layout);
  auto status=aether_layout_layout(engine,doc,map,float(viewport.width),float(viewport.height),nullptr,nullptr,&layout);
  if(status!=AETHER_OK){if(status==AETHER_ERROR_LIMIT_EXCEEDED)throw Error{status};if(bridge_error!=AETHER_OK)throw Error{bridge_error};core(status);}valid();
  #ifdef AETHER_HTML_LTR_TESTING
  testing::inspect(layout,nullptr);
#endif
  FinalTexts final;FinalControls controls;std::map<aether_node_t,std::string> collected;std::map<aether_node_t,float> baselines;std::vector<const aether_layout_box_s*> todo{layout->root};size_t placed=0;
  while(!todo.empty()){auto* box=todo.back();todo.pop_back();require(box);auto r=box->content_box;for(double d:{double(r.x),double(r.y),double(r.width),double(r.height)})if(!std::isfinite(d))throw Error{AETHER_ERROR_INTERNAL};
   if(box->is_atomic_control){
    require(profile==2&&owners.contains(box->element));const auto& o=owners.at(box->element);const auto& a=box->atomic_control;check_control(a.presentation,box->element,box->style);require(a.presentation==o.control&&box->control_display_text==o.control->text&&box->control_shows_placeholder==o.control->shows_placeholder);
    require(std::isfinite(r.width)&&std::isfinite(r.height)&&r.width>=0&&r.height>=0);
    aether_rectf rr{};if(o.control->text.empty()){require(!a.text.run&&!a.occurrence&&a.text.baseline==0);}else{
     const auto run=a.text.run;require(run&&occurrences.contains(run.get()));const auto& occ=occurrences.at(run.get());require(occ.owner==box->element&&occ.id==a.occurrence&&run->source==o.control->text&&a.text.baseline==run->metrics.baseline);
     rr={r.x,r.y+std::max(0.f,(r.height-run->metrics.height)/2.f),run->metrics.width,run->metrics.height};
     reserve_count(placed,run->glyphs.size(),16384);require(final.emplace(run.get(),FinalText{run,occ.product,rr,o.control->color,true}).second);
    }
    require(controls.emplace(box->element,FinalControl{a,r,rr}).second);collected[box->element]=o.control->text;
   }else {require(!box->atomic_control.presentation&&!box->atomic_control.text.run&&!box->atomic_control.occurrence);}
   if(box->kind==LAYOUT_BOX_TEXT){auto run=box->native_text.run;require(run&&occurrences.contains(run.get()));const auto& occurrence=occurrences.at(run.get());require(occurrence.owner==box->element&&run->source==box->text&&run->metrics.width==r.width&&run->metrics.height==r.height&&run->metrics.baseline==box->native_text.baseline);require(owners.at(box->element).record->style()==box->style.style_owner());
    const float baseline=r.y+run->metrics.baseline;if(!b2_enabled||control(box->element)){if(baselines.contains(box->element))require(baselines.at(box->element)==baseline);else baselines.emplace(box->element,baseline);}
    reserve_count(placed,run->glyphs.size(),16384);collected[box->element]+=box->text;require(final.emplace(run.get(),FinalText{run,occurrence.product,r,box->style.color,true}).second);
   }
   for(auto it=box->children.rbegin();it!=box->children.rend();++it)todo.push_back(*it);
  }
  for(size_t i=1;i<nodes.size();++i)if(!metadata(nodes[i])&&!inline_node(nodes[i])&&!br(nodes[i]))require(aether_layout_result_find_primary_box(layout,nodes[i]));
  for(const auto& [node,owner]:owners){if(owner.control)require(controls.contains(node));if(!b2_enabled||control(node))require(collected[node]==(owner.control?owner.control->text:visible.at(node)));}
  if(provenance)provenance->validate_final(layout);
  stage=AETHER_B0_STAGE_TRANSPORT;
  core(aether_display_list_builder_create(nullptr,nullptr,&builder));core(build_native_opacity(builder,layout,map,nullptr,paint));valid();
  #ifdef AETHER_HTML_LTR_TESTING
  testing::inspect(layout,paint.get());
#endif
  for(const auto& [node,o]:owners)if(o.control){aether::layout::BoxStyleSnapshot st(o.record->style(),o.record->measures());check_control(o.control,node,st);}
  masks=finalize_paint(*paint,final,fonts,controls,profile,"AetherB0");valid();HTML_LTR_STAGE(snapshot);auto snapshot=preparation::snapshot_native_opacity(*paint);
  if(auto e=std::get_if<preparation::Error>(&snapshot))throw Error{e->code==preparation::ErrorCode::OUT_OF_MEMORY?AETHER_ERROR_OUT_OF_MEMORY:e->code==preparation::ErrorCode::LIMIT_EXCEEDED?AETHER_ERROR_LIMIT_EXCEEDED:AETHER_ERROR_INTERNAL};
  stage=AETHER_B0_STAGE_PREPARATION;HTML_LTR_STAGE(preparation);auto frame=preparation::prepare(std::get<preparation::Snapshot>(snapshot));if(auto e=std::get_if<preparation::Error>(&frame))throw Error{e->code==preparation::ErrorCode::OUT_OF_MEMORY?AETHER_ERROR_OUT_OF_MEMORY:e->code==preparation::ErrorCode::LIMIT_EXCEEDED?AETHER_ERROR_LIMIT_EXCEEDED:AETHER_ERROR_INTERNAL};valid();prepared.emplace(std::get<preparation::PreparedFrame>(std::move(frame)));
 }
};
void B0SourceSession::install(aether_layout_engine_s* e,std::shared_ptr<HtmlLtrMeasureBridge> b){require(e&&!e->layout_in_progress&&!e->html_ltr_bridge_&&!e->text_selection_&&!e->synthetic_text_policy_&&!e->text_measurer&&!e->contextual_text_measurer);e->html_ltr_bridge_=std::move(b);}
B0SourceSession::B0SourceSession(std::string h,std::vector<uint8_t> r,std::vector<uint8_t> b,Viewport v):state_(std::make_unique<State>(std::move(h),std::move(r),std::move(b),v)){}
B0SourceSession::~B0SourceSession()=default;
const preparation::PreparedFrame& B0SourceSession::prepare(){try{state_->run();return *state_->prepared;}catch(const aether::layout::b2::LimitExceeded&){throw B0SourceError{AETHER_ERROR_LIMIT_EXCEEDED,state_->stage,AETHER_B0_REASON_LIMIT,state_->diagnostic_node};}catch(const aether::layout::b2::InvalidLedger&){throw B0SourceError{AETHER_ERROR_INTERNAL,state_->stage,AETHER_B0_REASON_INVARIANT,state_->diagnostic_node};}catch(const Error& e){throw B0SourceError{e.status,state_->stage,e.status==AETHER_ERROR_LIMIT_EXCEEDED?AETHER_B0_REASON_LIMIT:e.status==AETHER_ERROR_OUT_OF_MEMORY?AETHER_B0_REASON_ALLOCATION:e.status==AETHER_ERROR_UNSUPPORTED?AETHER_B0_REASON_UNSUPPORTED:AETHER_B0_REASON_INVARIANT,state_->diagnostic_node};}}
const std::vector<std::shared_ptr<const text::GlyphMaskA8>>& B0SourceSession::masks()const noexcept{return state_->masks;}
uint32_t B0SourceSession::diagnostic_stage()const noexcept{return state_->stage;}
uint64_t B0SourceSession::diagnostic_node()const noexcept{return state_->diagnostic_node;}
}
