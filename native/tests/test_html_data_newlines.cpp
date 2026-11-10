// Source normalization is Data-state only, before character-reference decoding.
#define AETHER_INLINE_UNITS_FIXTURE_ONLY
#include "test_inline_units.cpp"

static void bytes(aether_node_t n, const std::string& expected) {
 need(n != nullptr,"text owner exists");
 char* value=aether_node_get_text_content(n);
 check(value && expected==value,"exact DOM text bytes");
 aether_string_free(value);
}
static void dom_cases() {
 struct Sample {const char* source; const char* expected;};
 const Sample samples[]={
  {"",""},{"\r","\n"},{"\n","\n"},{"\r\n","\n"},{"\r\r","\n\n"},
  {"\r\r\n","\n\n"},{"\n\r","\n\n"},{"A\rB","A\nB"},
  {"\rA\r\nB\nC\r","\nA\nB\nC\n"},
  {"A&#13;B","A\rB"},{"A&#xD;B","A\rB"},{"A&#xd;B","A\rB"},
  {"A\r&#10;B","A\n\nB"},{"A&#13;\nB","A\r\nB"},
  {"A&#13;\r\nB","A\r\nB"},{"A\r&#xD;\nB","A\n\r\nB"},
  {"&amp;amp;\r&amp;#13;","&amp;\n&#13;"},
  {"A\r&lt;b id=phantom&gt;B&lt;/b&gt;","A\n<b id=phantom>B</b>"},
  {"A\r<3\nB","A\n<3\nB"},{"A\r<","A\n<"},
  {"\xc3\xa9\r\n\xe6\x9d\xb1\r\xf0\x9f\x98\x80","\xc3\xa9\n\xe6\x9d\xb1\n\xf0\x9f\x98\x80"}
 };
 aether_doc_t doc{};need(aether_doc_create(&doc)==AETHER_OK,"create reusable document");
 for(int repeat=0;repeat<3;++repeat)for(const auto&s:samples)for(bool eof:{false,true}){
  scenario="Data bytes/reparse/"+std::to_string(repeat)+(eof?"/EOF":"/close");
  const std::string html=std::string("<body><div id=target>")+s.source+(eof?"":"</div></body>");
  need(aether_doc_parse_html(doc,html.c_str())==AETHER_OK,"parse Data sample");
  auto target=aether_doc_get_element_by_id(doc,"target");bytes(target,s.expected);
  check(!aether_doc_get_element_by_id(doc,"phantom"),"decoded markup stays text");
 }
 for(const char* barrier:{"<span></span>","<!--x-->","<span><span></span></span>"}){
  scenario="markup barrier";
  std::string html=std::string("<body><div id=target>A\r")+barrier+"\nB</div></body>";
  need(aether_doc_parse_html(doc,html.c_str())==AETHER_OK,"parse barrier");
  auto target=aether_doc_get_element_by_id(doc,"target");
  // Read individual text nodes: comments may participate in legacy textContent.
  bytes(aether_node_get_child(target,0),"A\n");
  bytes(aether_node_get_child(target,2),"\nB");
 }
 scenario="scope controls";
 need(aether_doc_parse_html(doc,"<body><div id=target data-x='A\r\nB'><!--A\r\nB--><script id=s>A\r\nB&amp;</script><style id=c>A\rB</style><title id=t>A\r\nB&#13;</title><textarea id=a>\r\nA\rB&#13;</textarea></div></body>")==AETHER_OK,"parse controls");
 auto target=aether_doc_get_element_by_id(doc,"target");
 bytes(aether_node_get_child(target,0),"A\r\nB");
 bytes(aether_doc_get_element_by_id(doc,"s"),"A\r\nB&amp;");
 bytes(aether_doc_get_element_by_id(doc,"c"),"A\rB");
 bytes(aether_doc_get_element_by_id(doc,"t"),"A\nB\r");
 bytes(aether_doc_get_element_by_id(doc,"a"),"A\nB\r");
 char* attr=aether_element_get_attribute(target,"data-x");check(attr&&std::strcmp(attr,"A\r\nB")==0,"attribute source unchanged");aether_string_free(attr);
 need(aether_element_set_attribute(target,"data-x","C\r\nD&#13;")==AETHER_OK,"set attribute");
 attr=aether_element_get_attribute(target,"data-x");check(attr&&std::strcmp(attr,"C\r\nD&#13;")==0,"setter neither normalizes nor decodes");aether_string_free(attr);
 std::string source,expected;
 for(int i=0;i<8192;++i){source+="A\r\nB\r&#13;&amp;";expected+="A\nB\n\r&";}
 scenario="large source";std::string html="<body><div id=target>"+source+"</div></body>";
 need(aether_doc_parse_html(doc,html.c_str())==AETHER_OK,"large parse");bytes(aether_doc_get_element_by_id(doc,"target"),expected);
 need(aether_doc_parse_html(doc,"<body><div id=fresh>\nZ</div></body>")==AETHER_OK,"fresh reparse");
 check(!aether_doc_get_element_by_id(doc,"target"),"old ID removed");bytes(aether_doc_get_element_by_id(doc,"fresh"),"\nZ");
 aether_doc_destroy(doc);
}
static void geometry_cases(){
 struct Sample{const char* source;int lines;};
 const Sample samples[]={
  {"aa\rbb",2},{"aa\nbb",2},{"aa\r\nbb",2},{"aa\r\r\nbb",3},
  {"<span>aa\r</span><span>\nbb</span>",3},
  {"aa\r<!--x-->\nbb",3},{"aa\r<span></span>\nbb",3},
  {"aa\r&#10;bb",3}
 };
 for(const char* mode:{"normal","nowrap","pre","pre-line","pre-wrap"})for(const auto&s:samples)for(bool contextual:{false,true}){
  scenario=std::string("Data geometry/")+mode+(contextual?"/contextual":"/legacy");
  Fixture f(s.source,mode_css(mode,200),false,contextual);
  const bool collapse=std::strcmp(mode,"normal")==0||std::strcmp(mode,"nowrap")==0;
  near(f.rect().height,collapse?20:float(s.lines*20),"source newline height");
  const auto runs=f.runs();need(!runs.empty(),"visible fragments");
  near(runs.back().rect.y-f.rect().y,collapse?0:float((s.lines-1)*20),"last text baseline row");
  paint_matches(f);
 }
}
int main(){dom_cases();geometry_cases();std::printf("html-data-newlines: %d checks, %d failures\n",checks,failures);return failures?1:0;}
