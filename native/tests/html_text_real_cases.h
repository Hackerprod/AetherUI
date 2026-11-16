#pragma once
// Independently authored raw-admission corpus. Expected results are contract
// literals, never derived from the production scanner/parser.
#include <string>
#include <vector>
namespace q2c_cases {
struct Case { std::string name, html; bool accepted; std::string decoded; };
inline std::string style() {
 std::string s="font-family:aether-explicit;font-size:16px;line-height:64px;white-space:pre;font-weight:normal;font-style:normal;font-stretch:normal;font-kerning:none;font-variant-ligatures:none;direction:ltr;writing-mode:horizontal-tb;text-transform:none;text-align:left;text-indent:0px;letter-spacing:0px;word-spacing:0px;color:#000000;display:block;height:auto;width:128px";
 for(const auto& side:{"top","right","bottom","left"}){s+=";margin-"+std::string(side)+":0px;padding-"+side+":0px;border-"+side+"-width:0px;border-"+side+"-style:solid;border-"+side+"-color:#000000";}return s;
}
inline std::string owner(std::string text,std::string css=style()) {
 return "<div id=\"aether-text\" style=\""+css+"\">"+text+"</div>";
}
inline std::string document(std::string inner) {
 std::string css="display:block";
 for(const auto& side:{"top","right","bottom","left"})css+=";margin-"+std::string(side)+":0px;padding-"+side+":0px;border-"+side+"-width:0px";
 return "<html style=\""+css+"\"><body style=\""+css+"\">"+inner+"</body></html>";
}
inline std::vector<Case> corpus() {
 std::vector<Case> v;
 auto good=[&](std::string n,std::string t,std::string decoded){v.push_back({n,owner(t),true,decoded});};
 auto bad=[&](std::string n,std::string h){v.push_back({n,h,false,{}});};
 good("ascii","AB","AB");good("empty","","");good("space"," "," ");
 good("latin1","caf\xc3\xa9","caf\xc3\xa9");good("nbsp","A&nbsp;B","A\xc2\xa0" "B");
 good("named","&amp;&lt;&gt;&quot;&apos;","&<>\"'");
 good("decimal","&#65;&#233;","A\xc3\xa9");good("hex","&#x41;&#xE9;","A\xc3\xa9");
 good("pieces","A<!--middle-->B","AB");good("commentonly","<!--nothing-->","");
 good("repeat","AAAAAAAA","AAAAAAAA");good("spaces","  A  B  ","  A  B  ");
 good("punctuation","AV fi!?","AV fi!?");
 for(const auto& t:std::vector<std::string>{"<br>","<span>A</span>","<img>","<script>A</script>","<div>A</div>","\n","\t","\r","&unknown;","&amp","&#0;","&#xD800;","&#x110000;","&#999999999999999999999999;","&#x;","&#;","&#-1;","&#x1F600;","\xc3\x28","\xc0\xaf","\xe2\x82","\xc2\xad","\xc4\x80"})bad("text-invalid-"+std::to_string(v.size()),owner(t));
 for(const auto& css:std::vector<std::string>{"unknown:1","opacity:1","transform:none","display:flex","width:10%","width:calc(1px)","width:1.5px","margin:1px","font-size:17px","font-family:other","white-space:normal","font-weight:bold","font-style:italic","font-kerning:auto","direction:rtl","writing-mode:vertical-rl","text-align:center","letter-spacing:1px","word-spacing:1px","color:#000000!important","--x:1","padding:-1px","overflow:hidden","background-image:url(x)","w\\idth:1px","/*comment*/width:2px"})bad("css-invalid-"+std::to_string(v.size()),owner("A",style()+";"+css));
 for(const auto& prop:std::vector<std::string>{"font-family","font-size","line-height","white-space","font-weight","font-style","font-stretch","font-kerning","font-variant-ligatures","direction","writing-mode","text-transform","text-align","text-indent","letter-spacing","word-spacing","color"}){
   auto css=style();auto start=css.find(prop+":");auto end=css.find(';',start);css.erase(start,end==std::string::npos?css.size()-start:end-start+1);bad("missing-"+prop,owner("A",css));
 }
 auto h=owner("A");
 auto attr=[&](std::string n,std::string a){auto x=h;x.insert(4,a);bad(n,x);};
 attr("duplicate-id"," id=\"second\"");attr("case-duplicate-id"," ID=\"second\"");
 attr("duplicate-style"," style=\"unknown:1\"");attr("case-duplicate-style"," STYLE=\"\"");
 attr("unknown-attribute"," title=\"x\"");attr("event-handler"," onclick=\"x\"");
 bad("outside-space"," "+h);bad("outside-newline",h+"\n");bad("outside-text",h+"A");
 bad("double-owner",h+h);bad("missing-close",h.substr(0,h.size()-6));
 bad("wrong-close",h.substr(0,h.size()-6)+"</body>");
 bad("selfclosing-owner","<div id=\"aether-text\"/>");
 bad("unquoted-id","<div id=aether-text style=\""+style()+"\">A</div>");
 bad("unknown-owner",std::string("<div id=\"wrong\" style=\"")+style()+"\">A</div>");
 bad("unterminated-comment",owner("<!--oops"));
 bad("quoted-delimiter-attribute","<div id=\"aether-text\" title=\"><div>\" style=\""+style()+"\">A</div>");
 good("apos-numeric","&#39;&#x27;&apos;",std::string(3, char(39)));
 bad("duplicate-decoded-id","<div id=\"box\"></div><div id=\"b&#111;x\"></div>"+owner("A"));
 std::string encoded_style=style();for(unsigned i=0;i<800;++i)encoded_style+="&#32;";bad("raw-style-over4096",owner("A",encoded_style));
 for(auto& c:v)c.html=document(c.html);
 return v;
}
}
