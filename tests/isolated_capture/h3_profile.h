#pragma once
// Conservative admission for an explicitly enabled, initial-empty canvas snapshot.
// Not a CSS implementation or a source-coverage proof.
#include "../../native/src/layout/layout_internal.h"
#include "../../native/src/layout/initial_canvas.h"
#include "h5_profile.h"
#include <cmath>
#include <cstdlib>
#include <cerrno>
#include "../../native/src/css/border_utils.h"
namespace h3 {
inline std::string lower(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c+=32;return s;}
inline bool json(aether_node_s* n){auto t=n->attributes.find("type");return n->tag_name=="script"&&t!=n->attributes.end()&&lower(t->second)=="application/json"&&n->attributes.count("src")==0;}
// Only a single finite nonnegative px dimension (or unitless zero).
inline bool literal_limit(const std::string& value,size_t tokens){
 if(tokens!=1||value.empty())return false;
 size_t i=0;if(value[i]=='+'||value[i]=='-')++i;
 size_t digits=0;while(i<value.size()&&value[i]>='0'&&value[i]<='9'){++i;++digits;}
 if(i<value.size()&&value[i]=='.'){++i;size_t fractional=0;while(i<value.size()&&value[i]>='0'&&value[i]<='9'){++i;++fractional;}if(!fractional)return false;digits+=fractional;}
 if(!digits)return false;
 if(i<value.size()&&(value[i]=='e'||value[i]=='E')){++i;if(i<value.size()&&(value[i]=='+'||value[i]=='-'))++i;size_t exponent=0;while(i<value.size()&&value[i]>='0'&&value[i]<='9'){++i;++exponent;}if(!exponent)return false;}
 if(i<value.size()&&value.substr(i)!="px")return false;
 char* end=nullptr;errno=0;const float n=std::strtof(value.c_str(),&end);
 return errno!=ERANGE&&end!=value.c_str()&&std::isfinite(n)&&n>=0&&
   (std::string(end)=="px"||(*end=='\0'&&n==0));
}
// Restrictions on an original declaration list, not a stylesheet parser.
inline void authored_declarations(const std::string& s){
 css_variable_budget b{max_bytes*4,false};css_variable_lexer lex(s.data(),s.size(),0,&b);
 std::vector<css_variable_token> ts;
 for(;;){auto t=lex.next();require(!b.exhausted&&t.kind!=css_variable_token_kind::error,"CANVAS_AUTHORED_CSS_UNKNOWN");if(t.kind==css_variable_token_kind::end)break;if(t.kind!=css_variable_token_kind::whitespace&&t.kind!=css_variable_token_kind::comment)ts.push_back(t);}
 size_t i=0;
 auto delim=[&](size_t p,char c){return p<ts.size()&&ts[p].delimiter==c&&ts[p].serialization!=css_variable_serialization::string;};
 while(i<ts.size()){
  if(delim(i,';')){++i;continue;}
  require(ts[i].kind==css_variable_token_kind::ident,"CANVAS_AUTHORED_CSS_UNKNOWN");std::string key=lower(diagnostic::name(s,ts[i].begin,ts[i].end));++i;
  require(delim(i,':'),"CANVAS_AUTHORED_CSS_UNKNOWN");++i;std::string value;size_t nt=0;
  while(i<ts.size()&&!delim(i,';')&&!delim(i,'}')){require(!delim(i,'{'),"CANVAS_AUTHORED_CSS_UNKNOWN");auto t=ts[i++];
   if(t.kind==css_variable_token_kind::ident)value+=lower(diagnostic::name(s,t.begin,t.end));else value+=lower(s.substr(t.begin,t.end-t.begin));++nt;}
  require(nt>0,"CANVAS_AUTHORED_CSS_UNKNOWN");if(value.ends_with("!important")){value.resize(value.size()-10);nt-=2;}
  bool logical=key.find("inline-size")!=std::string::npos||key.find("block-size")!=std::string::npos||key.find("-inline")!=std::string::npos||key.find("-block")!=std::string::npos;
  require(key!="direction"&&key!="writing-mode"&&key!="aspect-ratio"&&key!="all"&&!logical,"CANVAS_AUTHORED_PROPERTY_UNSUPPORTED");
  if(key=="min-width"||key=="min-height")require(literal_limit(value,nt),"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");
  if(key=="max-width"||key=="max-height")require(value=="none"||literal_limit(value,nt),"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");
  if(key=="display")require(value!="contents","CANVAS_LAYOUT_PROFILE_UNSUPPORTED");
  if(key=="position")require(value!="sticky","CANVAS_LAYOUT_PROFILE_UNSUPPORTED");
  if(key=="align-items"||key=="align-self")require(value!="baseline"&&value!="firstbaseline"&&value!="lastbaseline","CANVAS_LAYOUT_PROFILE_UNSUPPORTED");
 }
}
inline void authored(const std::string& s,bool sheet,h5::WorkBudget* shared=nullptr){
 if(!sheet){authored_declarations(s);return;}
 // H5 owns rule structure and H7 owns keyframe semantics. Only original
 // declaration ranges flow into the canvas restrictions; execution sees s.
 const auto analysis=h5::analyze(s,true,shared);
 require(analysis.valid,"CANVAS_AUTHORED_CSS_UNKNOWN");
 for(const auto& span:analysis.declarations)
  authored_declarations(s.substr(span.begin,span.end-span.begin));
}
struct Style {
 aether_computed_style_t value{};
 Style(aether_style_map_t map,aether_node_s* node){auto h=aether_style_map_get(map,node);require(h!=nullptr,"CANVAS_STYLE_MISSING");auto rc=aether_computed_style_copy(h,&value);aether_computed_style_handle_destroy(h);require(rc==AETHER_OK,"CANVAS_STYLE_COPY_FAILED");}
 ~Style(){aether_computed_style_free(&value);}Style(const Style&)=delete;
};
inline bool px(aether_css_length v){return v.unit==AETHER_CSS_UNIT_PX&&std::isfinite(v.value)&&v.value>=0;}
// Runtime flat defaults leave absent min at AUTO; authored gate rejects explicit min:auto.
inline bool zero(aether_css_length v){return v.unit==AETHER_CSS_UNIT_AUTO||(px(v)&&v.value==0);}
inline bool auto_px(aether_css_length v){return v.unit==AETHER_CSS_UNIT_AUTO||px(v);}
inline bool limit(aether_css_length v){return v.unit==AETHER_CSS_UNIT_AUTO||px(v);}
inline float constrained_content(float content,aether_css_length lo,aether_css_length hi,float decoration,int sizing){
 require(limit(lo)&&limit(hi),"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");
 const float subtract=sizing==AETHER_BOX_SIZING_BORDER_BOX?decoration:0;
 const float minimum=lo.unit==AETHER_CSS_UNIT_AUTO?0:std::max(0.f,lo.value-subtract);
 const float maximum=hi.unit==AETHER_CSS_UNIT_AUTO?INFINITY:std::max(0.f,hi.value-subtract);
 return std::max(minimum,std::min(content,maximum));
}
inline float edge(aether_css_length v){require(px(v),"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");return v.value;}
inline float horizontal(aether_css_edges e){return edge(e.left)+edge(e.right);}
inline float vertical(aether_css_edges e){return edge(e.top)+edge(e.bottom);}
inline float margin_edge(aether_css_length v){require(v.unit==AETHER_CSS_UNIT_PX&&std::isfinite(v.value),"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");return v.value;}
inline float margin_h(aether_css_edges e){return margin_edge(e.left)+margin_edge(e.right);}
inline void margins(aether_css_edges e){margin_h(e);margin_edge(e.top);margin_edge(e.bottom);}
inline aether_css_edges borders(const aether_computed_style_t& s){auto e=s.border_width;
 if(!aether_border_style_occupies_space(s.border_styles.left))e.left=aether_css_length{0,AETHER_CSS_UNIT_PX,0,0,0,0,0};
 if(!aether_border_style_occupies_space(s.border_styles.right))e.right=aether_css_length{0,AETHER_CSS_UNIT_PX,0,0,0,0,0};
 if(!aether_border_style_occupies_space(s.border_styles.top))e.top=aether_css_length{0,AETHER_CSS_UNIT_PX,0,0,0,0,0};
 if(!aether_border_style_occupies_space(s.border_styles.bottom))e.bottom=aether_css_length{0,AETHER_CSS_UNIT_PX,0,0,0,0,0};
 return e;
}
inline void scripts(aether_style_map_t map,const std::vector<aether_node_s*>& nodes){
 for(auto* node:nodes){bool hidden=false;for(auto* n=node;n&&n->type!=AETHER_NODE_DOCUMENT;n=n->parent){Style s(map,n);if(s.value.display==AETHER_DISPLAY_NONE){hidden=true;break;}}require(hidden,"VISIBLE_SCRIPT_DATA_UNSUPPORTED");}
}
struct Expected {aether_node_s* node;float w,h;};
inline std::vector<Expected> before(aether_style_map_t map,const std::vector<aether_node_s*>& nodes){
 std::vector<Expected> out;
 for(auto* node:nodes){
  for(auto* n=node;n&&n->type!=AETHER_NODE_DOCUMENT;n=n->parent){Style st(map,n);require(st.value.display!=AETHER_DISPLAY_NONE,"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");require(st.value.transforms.count==0,"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");}
  Style st(map,node),parent(map,node->parent);const auto& s=st.value;const auto& p=parent.value;
  require((s.display==AETHER_DISPLAY_INLINE||s.display==AETHER_DISPLAY_INLINE_BLOCK||s.display==AETHER_DISPLAY_BLOCK)&&p.display==AETHER_DISPLAY_FLEX&&p.flex_direction==AETHER_FLEX_DIRECTION_ROW&&!p.flex_wrap&&px(p.width),"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");
  int align=s.align_self==AETHER_ALIGN_SELF_AUTO?p.align_items:s.align_self;
  require((align==AETHER_ALIGN_START||align==AETHER_ALIGN_CENTER)&&s.position==AETHER_POSITION_STATIC&&s.flex_grow==0&&s.flex_basis.kind==AETHER_FLEX_BASIS_AUTO&&auto_px(s.width)&&auto_px(s.height)&&limit(s.min_width)&&limit(s.min_height)&&limit(s.max_width)&&limit(s.max_height),"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");
  margins(s.margin);
  auto dim=[&](const char* key,uint32_t fallback){auto at=node->attributes.find(key);return initial_canvas_dimension(at==node->attributes.end()?nullptr:&at->second,fallback);};
  float iw=dim("width",300),ih=dim("height",150);require(iw<=8192&&ih<=8192,"CANVAS_DIMENSION_LIMIT");
  float dx=horizontal(s.padding)+horizontal(borders(s)),dy=vertical(s.padding)+vertical(borders(s));
  require(s.box_sizing==AETHER_BOX_SIZING_CONTENT_BOX||s.box_sizing==AETHER_BOX_SIZING_BORDER_BOX,"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");
  bool aw=s.width.unit==AETHER_CSS_UNIT_AUTO,ah=s.height.unit==AETHER_CSS_UNIT_AUTO;
  require((!aw&&!ah)||(zero(s.min_width)&&zero(s.min_height)&&s.max_width.unit==AETHER_CSS_UNIT_AUTO&&s.max_height.unit==AETHER_CSS_UNIT_AUTO),"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");
  require(aw==ah||(iw>0&&ih>0),"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");
  float w=aw?iw:std::max(0.f,s.width.value-(s.box_sizing==AETHER_BOX_SIZING_BORDER_BOX?dx:0));
  float h=ah?ih:std::max(0.f,s.height.value-(s.box_sizing==AETHER_BOX_SIZING_BORDER_BOX?dy:0));
  if(aw&&!ah)w=h*iw/ih;
  if(ah&&!aw)h=w*ih/iw;
  if(!aw&&!ah){w=constrained_content(w,s.min_width,s.max_width,dx,s.box_sizing);h=constrained_content(h,s.min_height,s.max_height,dy,s.box_sizing);}
  require(std::isfinite(w)&&std::isfinite(h),"CANVAS_DIMENSION_LIMIT");out.push_back({node,w,h});
 }
 return out;
}
inline void after(aether_layout_result_t result,aether_style_map_t map,const std::vector<Expected>& expected){
 std::set<aether_node_s*> parents;size_t i=0;
 for(const auto& e:expected){auto* box=result->find_primary_box(e.node);require(box&&box->children.empty(),"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");
  require(std::abs(box->content_box.width-e.w)<0.01f&&std::abs(box->content_box.height-e.h)<0.01f,"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");
  parents.insert(e.node->parent);logstream<<"canvas_"<<i++<<"_content="<<box->content_box.x<<","<<box->content_box.y<<","<<box->content_box.width<<","<<box->content_box.height<<"\n";
 }
 // Prove no shrink by recomputing the outer main bases for every direct flex item.
 for(auto* parent:parents){auto* pb=result->find_primary_box(parent);require(pb!=nullptr,"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");Style pst(map,parent);double total=0;size_t count=0;
  for(auto* child:parent->children){if(child->type==AETHER_NODE_TEXT){require(child->text.find_first_not_of(" \t\r\n\f")==std::string::npos,"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");continue;}
   Style st(map,child);const auto& s=st.value;if(s.display==AETHER_DISPLAY_NONE)continue;
   require(s.position==AETHER_POSITION_STATIC&&s.flex_grow==0&&s.flex_basis.kind==AETHER_FLEX_BASIS_AUTO,"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");
   float base=0;auto found=std::find_if(expected.begin(),expected.end(),[&](const Expected& e){return e.node==child;});
   if(found!=expected.end())base=found->w+horizontal(s.padding)+horizontal(borders(s));
   else {
    require(px(s.width)&&limit(s.min_width)&&limit(s.max_width)&&
      (s.box_sizing==AETHER_BOX_SIZING_CONTENT_BOX||s.box_sizing==AETHER_BOX_SIZING_BORDER_BOX),"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");
    const float decoration=horizontal(s.padding)+horizontal(borders(s));
    const float content=s.box_sizing==AETHER_BOX_SIZING_CONTENT_BOX?s.width.value:std::max(0.f,s.width.value-decoration);
    base=constrained_content(content,s.min_width,s.max_width,decoration,s.box_sizing)+decoration;
   }
   // A hypothetical main size is not proof of final geometry: reject any
   // changed sibling as well as canvas (including content-driven expansion).
   auto* cb=result->find_primary_box(child);
   require(cb&&std::isfinite(base)&&std::abs(cb->border_box.width-base)<0.01f,"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");
   total+=base+margin_h(s.margin);++count;
  }
  // Use the same frozen typed column-gap resolution as layout, not the
  // legacy scalar gap (which loses row/column and percentage information).
  StyleLookup lookup(result->assets,{},false);
  float gap=layout_used_gap(&lookup,parent,pb->style,false,pb->content_box.width,true);
  require(std::isfinite(gap)&&gap>=0,"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");if(count)total+=(count-1)*gap;
  require(total<=pb->content_box.width+0.01,"CANVAS_LAYOUT_PROFILE_UNSUPPORTED");
 }
 logstream<<"canvas_count="<<expected.size()<<"\ncanvas_bitmap_bytes=0\ncanvas_content_commands=0\n";
}
}
