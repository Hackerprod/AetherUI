#pragma once
#include "session.h"
#include "admission.h"
#include "../../layout/layout_internal.h"
#include <map>
#include <string_view>
namespace aether::rendering::html_ltr::session_validation {
inline void require(bool v){if(!v)throw Error{AETHER_ERROR_INTERNAL};}
inline void core(aether_status_t s){if(s!=AETHER_OK)throw Error{s==AETHER_ERROR_OUT_OF_MEMORY?s:AETHER_ERROR_INTERNAL};}
inline constexpr const char* ua=
 "html,body,div,h1,p {display:block;position:static;box-sizing:content-box;width:auto;height:auto;margin-top:0px;margin-right:0px;margin-bottom:0px;margin-left:0px;padding-top:0px;padding-right:0px;padding-bottom:0px;padding-left:0px;border-width:0px;border-style:none;overflow:visible;opacity:1;background-color:transparent;}"
 "html {overflow-x:clip;overflow-y:clip;font-family:AetherLTR;font-size:16px;font-weight:400;line-height:24px;color:#202020;white-space:nowrap;text-align:start;direction:ltr;writing-mode:horizontal-tb;}";
inline aether_color_rgba color(unsigned rgb){return {float((rgb>>16)&255)/255,float((rgb>>8)&255)/255,float(rgb&255)/255,1};}
inline bool same(aether_color_rgba a,aether_color_rgba b){return a.r==b.r&&a.g==b.g&&a.b==b.b&&a.a==b.a;}
inline unsigned number(const std::string& s){unsigned n=0;for(char c:s){if(c<'0'||c>'9')break;n=n*10+unsigned(c-'0');}return n;}
inline unsigned rgb(const std::string& s){unsigned n=0;for(size_t i=1;i<s.size();++i){char c=s[i];n=n*16+unsigned(c<='9'?c-'0':(c|32)-'a'+10);}return n;}
inline Inventory admit_startup(std::string_view html,size_t font_bytes,uint32_t profile){
 if(font_bytes>16u*1024u*1024u||html.size()>65536)throw Error{AETHER_ERROR_LIMIT_EXCEEDED};
 // Reserve a conservative 1 MiB inventory/session allowance before recognition.
 // Together with copied inputs this is below the 32 MiB logical session quota;
 // FontProvider receives the same reservation before any subsequent H work.
 if(font_bytes+html.size()+1024u*1024u>32u*1024u*1024u)throw Error{AETHER_ERROR_LIMIT_EXCEEDED};
 HTML_LTR_STAGE(inventory);
 return admit(html,profile);
}
inline bool trivia_same(std::string_view authored,std::string_view parsed) {
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
inline void verify_style(aether_style_map_t map,aether_node_t node,const Node& authored){
 auto handle=aether_style_map_get(map,node);require(handle);Flat flat;auto code=aether_computed_style_copy(handle,&flat.s);aether_computed_style_handle_destroy(handle);core(code);const auto& s=flat.s;
 const auto& e=authored.inherited;
 require(s.font_size==e.font_size&&s.font_weight==int(e.font_weight)&&s.line_height==e.line_height&&same(s.color,color(e.color_rgb))&&std::string_view(s.font_family)=="AetherLTR");
 require(s.display==AETHER_DISPLAY_BLOCK&&s.position==AETHER_POSITION_STATIC&&s.box_sizing==AETHER_BOX_SIZING_CONTENT_BOX&&s.height.unit==AETHER_CSS_UNIT_AUTO&&s.opacity==1&&s.visible&&s.white_space==AETHER_WHITE_SPACE_NOWRAP&&s.text_align==AETHER_TEXT_ALIGN_START&&s.text_transform==AETHER_TEXT_TRANSFORM_NONE&&s.overflow_x==(authored.tag=="html"?AETHER_OVERFLOW_CLIP:AETHER_OVERFLOW_VISIBLE)&&s.overflow_y==(authored.tag=="html"?AETHER_OVERFLOW_CLIP:AETHER_OVERFLOW_VISIBLE));
 require(!s.has_text_gradient&&(s.background_layers.count==0||(s.background_layers.count==1&&((s.background_layers.items[0].kind==AETHER_BACKGROUND_LAYER_IMAGE_URL&&s.background_layers.items[0].image_source&&*s.background_layers.items[0].image_source)||s.background_layers.items[0].kind==AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT)))&&!s.backdrop_filters.count&&!s.transforms.count&&!s.text_shadows.count&&!s.transition_properties&&!s.transition_duration&&!s.transition_delay&&s.z_index_kind==AETHER_Z_INDEX_AUTO);
 // Ciclo 04 (Cambio 2): box_shadows admitidas exigiendo inset == FALSE por
 // sombra (sin re-chequeo de topes: los garantiza css(), §2.5). `inset` es
 // el único caso de pérdida silenciosa en el builder; si una futura
 // modificación lo admitiera en css(), la página fallaría en voz alta aquí.
 for(int32_t i=0;i<s.box_shadows.count;++i)require(s.box_shadows.items[i].inset==AETHER_FALSE);
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

inline void verify_dom(aether_doc_t doc,const Inventory& inventory,std::vector<aether_node_t>& nodes){
  nodes.resize(inventory.nodes.size());std::vector<std::pair<aether_node_t,size_t>> pending{{aether_doc_get_document_element(doc),0}};
  while(!pending.empty()){auto [node,idx]=pending.back();pending.pop_back();const auto& authored=inventory.nodes[idx];require(node&&node->type==AETHER_NODE_ELEMENT&&node->tag_name==authored.tag&&!node->is_generated);nodes[idx]=node;
   if(authored.tag=="input"||authored.tag=="button"){
    const auto attribute=[&](const char* name){auto it=node->attributes.find(name);return it==node->attributes.end()?std::string():it->second;};
    require(attribute("type")==authored.type&&attribute("value")==authored.value&&attribute("placeholder")==authored.placeholder);
    if(authored.tag=="input"){require(node->children.empty());continue;}
   }
   if(authored.leaf()){require(node->children.size()==(authored.text.empty()?0u:1u));if(!authored.text.empty())require(node->children[0]->type==AETHER_NODE_TEXT&&node->children[0]->text==authored.text);continue;}
   size_t child=0,trivia=0;
   for(auto* n:node->children){if(n->type==AETHER_NODE_TEXT){require(trivia<authored.trivia.size()&&authored.trivia[trivia].before_child==child&&trivia_same(authored.trivia[trivia].text,n->text));++trivia;}else{require(n->type==AETHER_NODE_ELEMENT&&child<authored.children.size());pending.push_back({n,authored.children[child++]});}}
   require(child==authored.children.size()&&trivia==authored.trivia.size());
  }
  for(auto n:nodes)require(n);
  require(aether_doc_get_body(doc)==nodes[1]);
 }
}
