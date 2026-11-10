// HTML input preprocessing precedes every tokenizer state and reference decode.
#include <aether/aether.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
static int failures=0, checks=0;
static void check(bool ok,const char* context){++checks;if(!ok){++failures;std::fprintf(stderr,"FAIL %s\n",context);}}
static std::string take(char* p){check(p!=nullptr,"owned string");std::string s=p?p:"";aether_string_free(p);return s;}
static std::string text(aether_node_t n){if(!n)return "";std::string out=take(aether_node_get_text_content(n));for(int i=0;i<aether_node_get_child_count(n);++i)out+=text(aether_node_get_child(n,i));return out;}
int main(){
 aether_doc_t d{};check(aether_doc_create(&d)==AETHER_OK,"create");
 struct Sample{const char* source;const char* normalized;};
 const Sample samples[]={{"",""},{"\r","\n"},{"\r\n","\n"},{"\r\r\n","\n\n"},{"\n\r","\n\n"},{"A\rB\r\nC\n","A\nB\nC\n"},{"\xc3\xa9\r\n\xe6\x9d\xb1\r\xff","\xc3\xa9\n\xe6\x9d\xb1\n\xff"}};
 for(int repeat=0;repeat<2;++repeat)for(auto s:samples)for(const char* tag:{"div","title","textarea","script","style","comment"})for(bool eof:{false,true}){
  bool comment=std::strcmp(tag,"comment")==0;
  std::string input=comment?"<!--":std::string("<")+tag+" id=t>";
  input+=s.source;
  if(!eof)input+=comment?"-->":std::string("</")+tag+">";
  check(aether_doc_parse_html(d,input.c_str())==AETHER_OK,"parse matrix");
  input.assign(input.size(),'x');input.clear();input.shrink_to_fit();
  auto n=comment?aether_node_get_child(reinterpret_cast<aether_node_t>(d),0):aether_doc_get_element_by_id(d,"t");
  check(n!=nullptr,"matrix owner");std::string expected=s.normalized;
  if(std::strcmp(tag,"textarea")==0&&!expected.empty()&&expected[0]=='\n')expected.erase(0,1);
  check(text(n)==expected,tag);
 }
 for(const char* tag:{"div","title","textarea","script","style","comment"}){
  bool comment=std::strcmp(tag,"comment")==0;
  std::string input=comment?"<!--":std::string("<")+tag+" id=t>";
  input+="A\r&#10;B&#13;\nC&#xD;&amp;&lt;b&gt;"; input+=comment?"-->":std::string("</")+tag+">";
  check(aether_doc_parse_html(d,input.c_str())==AETHER_OK,"parse references");
  bool literal=comment||std::strcmp(tag,"script")==0||std::strcmp(tag,"style")==0;
  check(text(comment?aether_node_get_child(reinterpret_cast<aether_node_t>(d),0):aether_doc_get_element_by_id(d,"t"))==(literal?"A\n&#10;B&#13;\nC&#xD;&amp;&lt;b&gt;":"A\n\nB\r\nC\r&<b>"),"reference boundary");
 }
 for(const char* prefix:{"\r","\r\n","&#10;","&#13;"}){
  std::string input=std::string("<textarea id=t>")+prefix+"X</textarea>";
  check(aether_doc_parse_html(d,input.c_str())==AETHER_OK,"textarea initial");
  check(text(aether_doc_get_element_by_id(d,"t"))==(std::strcmp(prefix,"&#13;")==0?"\rX":"X"),"first decoded LF only");
 }
 for(char quote:{'\'', '"'})for(auto s:samples){
  std::string input="<div id=t data-x=";input+=quote;input+=s.source;input+="&#13;\r&#10;";input+=quote;input+=" DATA-X=ignored data-u=A\r\n data-v=B\r>tail</div>";
  check(aether_doc_parse_html(d,input.c_str())==AETHER_OK,"attributes");auto n=aether_doc_get_element_by_id(d,"t");
  check(take(aether_element_get_attribute(n,"data-x"))==std::string(s.normalized)+"\r\n\n","quoted attr + duplicate");
  check(take(aether_element_get_attribute(n,"data-u"))=="A","unquoted CRLF delimiter");check(take(aether_element_get_attribute(n,"data-v"))=="B","unquoted CR delimiter");
 }
 for(const char* barrier:{"<b></b>","<!--x-->"}){
  std::string input=std::string("<div id=t>A\r")+barrier+"\nB</div>";
  check(aether_doc_parse_html(d,input.c_str())==AETHER_OK,"barrier");auto n=aether_doc_get_element_by_id(d,"t");
  check(text(aether_node_get_child(n,0))=="A\n"&&text(aether_node_get_child(n,2))=="\nB","no pair across markup");
 }
 for(const char* tag:{"script","style","title","textarea"}){
  std::string input=std::string("<")+tag+" id=t>A\r\n</"+tag+"\r\n x='>\r\n'>tail<p id=after>Z</p>";
  check(aether_doc_parse_html(d,input.c_str())==AETHER_OK,"end tag suffix");check(text(aether_doc_get_element_by_id(d,"t"))=="A\n","normalized delimiter indexes");check(aether_doc_get_element_by_id(d,"after")!=nullptr,"tail remains markup");
 }
 const char nul[]="<div id=t>A\r\n\0ignored</div>";
 check(aether_doc_parse_html(d,nul)==AETHER_OK,"embedded NUL boundary");check(text(aether_doc_get_element_by_id(d,"t"))=="A\n","NUL remains EOF");
 check(aether_doc_parse_html(d,"<p id=fresh>Z</p>")==AETHER_OK,"fresh replacement");check(!aether_doc_get_element_by_id(d,"t"),"old ID removed");
 aether_doc_destroy(d);std::printf("html-source-newlines: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
