#include "../src/rendering/html_ltr/admission.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
using namespace aether::rendering::html_ltr;
static unsigned checks;
static std::string page(const std::string& body){return "<html><body>"+body+"</body></html>";}
static void expect(const std::string& body,aether_status_t expected,unsigned profile=2){
 aether_status_t actual=AETHER_OK;
 try{(void)admit(page(body),profile);}catch(const Error& e){actual=e.status;}
 ++checks;if(actual!=expected){std::cerr<<"case "<<checks<<" expected "<<expected<<" got "<<actual<<'\n';std::abort();}
}
int main(){
 const auto invalid=AETHER_ERROR_INVALID_ARGUMENT,unsupported=AETHER_ERROR_UNSUPPORTED,limit=AETHER_ERROR_LIMIT_EXCEEDED;
 std::string attributes[]={"type=\"text\"","value=\" Ana  \"","placeholder=\" Usuario  \"","style=\"width:200px;\""};
 std::sort(std::begin(attributes),std::end(attributes));
 do{std::string tag="<input";for(const auto& a:attributes)tag+="  "+a;tag+=" >";expect(tag,AETHER_OK);auto i=admit(page(tag),2);assert(i.nodes[2].value==" Ana  "&&i.nodes[2].placeholder==" Usuario  "&&i.control_count==1&&i.scalar_count==16);}while(std::next_permutation(std::begin(attributes),std::end(attributes)));
 expect("<input type=\"text\">",AETHER_OK);
 expect("<input type=\"password\">",AETHER_OK);
 expect("<button type=\"button\">Entrar</button>",AETHER_OK);
 expect("<input type=\"text\">",unsupported,1);
 expect("<button type=\"button\">Entrar</button>",unsupported,1);
 for(const auto& b:{"<input>","<input type=\"text\"/>","<input type=\"text\" />","<input type=\"text\"></input>","<input type=\"text\" type=\"text\">","<input type='text'>","<input type=text>","<input type =\"text\">","<input type= \"text\">","<input\ttype=\"text\">","<input type=\"text\"\nvalue=\"x\">","<button type=\"button\"></button>","<button>Entrar</button>"})expect(b,invalid);
 for(const auto& b:{"<input type=\"checkbox\">","<input type=\"TEXT\">","<input type=\"text\" disabled=\"true\">","<input type=\"text\" value=\"&\">","<input type=\"text\" value=\"<\">","<input type=\"text\" placeholder=\"•\">","<button type=\"submit\">Entrar</button>","<button type=\"button\"> Entrar</button>","<button type=\"button\">Entrar  ahora</button>","<button type=\"button\"><p>Entrar</p></button>","<textarea></textarea>"})expect(b,unsupported);
 for(const auto& attr:{"value","placeholder"}){
  const std::string start=std::string("<input type=\"password\" ")+attr+"=\"";
  for(const auto& malformed:{std::string("\0",1),std::string("\xc0\x80",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\xc3",1)})expect(start+malformed+"\">",invalid);
  for(const auto& outside:{"\t","\r","\n","😀","•","\xc2\xad"})expect(start+outside+"\">",unsupported);
  expect(start+std::string(256,'a')+"\">",AETHER_OK);expect(start+std::string(257,'a')+"\">",limit);
 }
 auto accented=admit(page("<input type=\"password\" value=\" á  é \" placeholder=\"oculto\">"),2);assert(accented.scalar_count==12);++checks;
 std::string controls;for(int i=0;i<32;++i)controls+="<input type=\"text\">";expect(controls,AETHER_OK);expect(controls+"<input type=\"text\">",limit);
 std::string quotas;for(int i=0;i<8;++i)quotas+="<input type=\"password\" value=\""+std::string(256,'a')+"\" placeholder=\""+std::string(256,'b')+"\">";expect(quotas,AETHER_OK);expect(quotas+"<p>x</p>",limit);
 expect("<input type=\"text\" style=\"width:0px;\">",limit);
 expect("<input type=\"text\" style=\"width:1px;\">",AETHER_OK);
 expect("<input type=\"text\" style=\"height:20px;\">",unsupported);
 expect("<p>Ana</p><input type=\"text\" value=\"Ana\"><button type=\"button\">Ana</button>",AETHER_OK);
 std::cout<<"admission static controls: "<<checks<<" checks PASS\n";
}
