#include "../src/layout/inline_text.h"
#include <cassert>
#include <string>
#include <cstdio>
int main() {
 unsigned checks=0;
 for(unsigned cp=0;cp<256;++cp) {
  std::string s;
  if(cp<128) s.push_back(char(cp));
  else {s.push_back(char(0xc0|(cp>>6)));s.push_back(char(0x80|(cp&63)));}
  const bool ws=cp==32||cp==9||cp==10||cp==13||cp==12;
  const bool expected=ws||(cp>=32&&cp<=126)||(cp>=160&&cp!=173);
  assert(aether_b2_inline_text_supported(s)==expected);++checks;
 }
 for(const auto& s : {std::string("\xc2"), std::string("\xc3"),std::string("\x80"),std::string("\xc0\xa0"),std::string("\xc2\x20"),std::string("\xe2\x80\xae"),std::string("\xf0\x9f\x98\x80")}) {
  assert(!aether_b2_inline_text_supported(s));++checks;
 }
 assert(aether_b2_inline_text_supported("¿Qué tal? ¡Sí!\t\r\n\f"));++checks;
 assert(!aether_b2_inline_space_byte(0xa0));++checks;
 assert(!aether_b2_inline_space_byte(11));++checks;
 for(const char c : std::string(" \t\r\n\f")) {assert(aether_b2_inline_space_byte(c));++checks;}
 for(int mode : {AETHER_WHITE_SPACE_NORMAL,AETHER_WHITE_SPACE_NOWRAP}) {
  const auto v=aether_tokenize_inline_text("¿Qué tal?\t\r\n\f¡Sí!",mode);
  assert(v.size()==3&&v[0].text=="¿Qué tal?"&&v[1].text==" "&&v[2].text=="¡Sí!");++checks;
 }
 // Opt-in repertoire helpers do not alter the legacy VT tokenizer contract.
 const auto legacy=aether_tokenize_inline_text("a\vb",AETHER_WHITE_SPACE_NORMAL);
 assert(legacy.size()==3&&legacy[1].text==" ");++checks;
 std::printf("B2 repertoire preparation: %u checks PASS\n",checks);
}
