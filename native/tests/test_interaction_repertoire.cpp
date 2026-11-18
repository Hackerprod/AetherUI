#include "rendering/html_ltr/admission.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace aether::rendering::html_ltr;
static void invalid(std::string s){std::size_t i=0;bool rejected=false;try{while(i<s.size())(void)decode_scalar_utf8(s,i);}catch(const Error& e){assert(e.status==AETHER_ERROR_INVALID_ARGUMENT);rejected=true;}assert(rejected);}
int main(){
 for(std::uint32_t cp=0;cp<=0x10ffff;++cp)assert(repertoire_scalar(cp)==((cp>=32&&cp<=126)||(cp>=160&&cp<=255&&cp!=173)));
 assert(!repertoire_scalar(0xffffffff));
 for(std::uint32_t cp=1;cp<=255;++cp){std::string s;if(cp<128)s.push_back(char(cp));else{s.push_back(char(0xc0|(cp>>6)));s.push_back(char(0x80|(cp&63)));}std::size_t i=0;assert(decode_scalar_utf8(s,i)==cp&&i==s.size());}
 invalid(std::string(1,'\0'));invalid("\xc0\xaf");invalid("\xc2");invalid("\xed\xa0\x80");invalid("\xf4\x90\x80\x80");invalid("\x80");invalid("\xe0\x80\xaf");invalid("\xf0\x80\x80\xaf");invalid("a\xe2\x80");
 std::size_t i=0;assert(decode_scalar_utf8("\xe2\x80\xa2",i)==0x2022);assert(!repertoire_scalar(0x2022));
 // Literal runtime repertoire is separate from authored syntax admission.
 for(char c:std::string("<>&\"'"))assert(repertoire_scalar(static_cast<unsigned char>(c)));
 bool rejected=false;try{(void)admit("<html><body><input type=\"text\" value=\"&lt;\"></body></html>",2);}catch(const Error& e){assert(e.status==AETHER_ERROR_UNSUPPORTED);rejected=true;}assert(rejected);
 std::puts("shared UTF8/repertoire PASS (not runtime dispatch validation)");
}
