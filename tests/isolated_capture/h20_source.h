#pragma once
// H20 authored proof is deliberately narrower than the native direction parser.
// Every declaration is audited, including overridden/inactive/no-match rules.
namespace h20 {
// The native legacy keyword handlers are not uniformly escape-normalized.
// The capture profile admits literal lowercase tokens only; do not decode a
// spelling here that the consuming native property might silently ignore.
inline bool keyword(std::string_view value,std::initializer_list<std::string_view> allowed) {
 for(auto a:allowed)if(value==a)return true;
 return false;
}
inline bool direction(std::string_view value){return keyword(value,{"ltr","rtl","initial","inherit","unset"});}
// Closed name AND value admission, based on the native registered physical
// properties exercised by H20. No unknown/vendor/alias default acceptance.
// Use the same complete literal parser as native lengths, then constrain the
// capture domain well inside float limits. No numeric functions/percentages
// are admitted. With <=2048 boxes, bounded dimensions/margins/flex factors do
// not approach float overflow in placement sums or flex weight products.
inline bool bounded(double n,bool negative) {
 const double magnitude=std::abs(n);
 return std::isfinite(n)&&(negative||n>=0)&&magnitude<=65536.&&
   (magnitude==0||magnitude>=0.000001);
}
inline bool scalar(const std::string& s,bool negative=false) {
 aether::css::calc_value_s parsed;double original{};
 return aether::css::try_parse_numeric_literal(s,parsed,&original)&&
  parsed.kind==aether::css::calc_value_kind_e::number&&bounded(original,negative);
}
inline bool px(const std::string& s,bool negative=false) {
 aether::css::calc_value_s parsed;double original{};
 if(!aether::css::try_parse_numeric_literal(s,parsed,&original)||!bounded(original,negative))return false;
 if(parsed.kind==aether::css::calc_value_kind_e::number)return original==0;
 return s.ends_with("px")&&parsed.length.unit==AETHER_CSS_UNIT_PX&&std::isfinite(parsed.length.value);
}
inline bool color(const std::string& s) {
 if(keyword(s,{"transparent","black","white","red","green","blue","yellow"}))return true;
 if(s.empty()||s[0]!='#'||(s.size()!=4&&s.size()!=7))return false;
 for(size_t i=1;i<s.size();++i)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')||(s[i]>='A'&&s[i]<='F')))return false;
 return true;
}
inline bool source(const std::string& property,const std::string& value){
 if(property.starts_with("--"))return true; // definitions are data; consumers audited
 // No escaped keywords, comment-dependent token joining, or alternate lexical
 // serialization is proved for any non-custom consumer, even if CSS-valid.
 if(value.find('\\')!=std::string::npos||value.find('/')!=std::string::npos)return false;
 if(property=="direction")return direction(value);
 if(property=="writing-mode")return keyword(value,{"horizontal-tb","vertical-rl","vertical-lr"});
 if(property=="display")return keyword(value,{"block","flex","none","initial","inherit","unset"});
 if(property=="flex-direction")return keyword(value,{"row","row-reverse","column","column-reverse","initial","inherit","unset"});
 if(property=="flex-wrap")return keyword(value,{"nowrap","wrap","initial"});
 if(property=="justify-content")return keyword(value,{"start","end","flex-start","flex-end","center","space-between","initial","inherit","unset"});
 if(property=="align-items")return keyword(value,{"start","end","flex-start","flex-end","center","stretch","initial"});
 if(property=="align-self")return keyword(value,{"auto","start","end","flex-start","flex-end","center","stretch","initial"});
 if(property=="align-content")return keyword(value,{"stretch","initial"});
 if(property=="position")return keyword(value,{"static","initial"});
 if(property=="overflow"||property=="overflow-x"||property=="overflow-y")return keyword(value,{"visible","initial"});
 if(property=="box-sizing")return keyword(value,{"content-box","border-box"});
 if(property=="background"||property=="background-color"||property=="color")return color(value);
 // Size grammars are property-specific. Native min-size does not yet
 // implement CSS auto, while native max-size accepts non-CSS auto. Neither
 // behavior is evidence for capture admission: intersect CSS and consumer.
 if(property=="width"||property=="height"||property=="flex-basis")
  return px(value)||keyword(value,{"auto"});
 if(property=="min-width"||property=="min-height")return px(value);
 if(property=="max-width"||property=="max-height")return px(value)||keyword(value,{"none"});
 if(property=="flex-grow"||property=="flex-shrink")return scalar(value);
 if(property=="order") {
  size_t i=(!value.empty()&&(value[0]=='+'||value[0]=='-'))?1:0;
  if(i==value.size())return false;
  for(;i<value.size();++i)if(value[i]<'0'||value[i]>'9')return false;
  aether::css::c_numeric_locale locale;auto n=locale.parse_double(value);
  return n.valid&&!n.range_error&&n.value>=-2147483648.&&n.value<=2147483647.;
 }
 bool valid=false;auto parts=css_split_components(value,&valid);if(!valid||parts.empty())return false;
 if(property=="flex")return keyword(value,{"none","auto"})||scalar(value)||(parts.size()==3&&scalar(parts[0])&&scalar(parts[1])&&(px(parts[2])||keyword(parts[2],{"auto"})));
 if(property=="gap"||property=="row-gap"||property=="column-gap") {
  if(parts.size()>(property=="gap"?2:1))return false;
  for(const auto& part:parts)if(!px(part))return false;
  return true;
 }
 const bool margin=property=="margin"||property=="margin-left"||property=="margin-right"||property=="margin-top"||property=="margin-bottom";
 const bool padding=property=="padding"||property=="padding-left"||property=="padding-right"||property=="padding-top"||property=="padding-bottom";
 if(margin||padding) {
  if(parts.size()>((property=="margin"||property=="padding")?4:1))return false;
  for(const auto& part:parts)if(!px(part,margin)&&!(margin&&keyword(part,{"auto"})))return false;
  return true;
 }
 for(auto side:{"", "-left", "-right", "-top", "-bottom"}) {
  const std::string base=std::string("border")+side;
  if(property==base)return parts.size()==3&&px(parts[0])&&keyword(parts[1],{"solid","none","hidden"})&&color(parts[2]);
  if(property==base+"-width"||property==base+"-style"||property==base+"-color") {
   if(parts.size()>(side[0]?1:4))return false;
   for(const auto& part:parts) {
    if(property.ends_with("-width")&&!px(part))return false;
    if(property.ends_with("-style")&&!keyword(part,{"solid","none","hidden"}))return false;
    if(property.ends_with("-color")&&!color(part))return false;
   }
   return true;
  }
 }
 return false;
}
}
