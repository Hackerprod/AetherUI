#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/rendering/text/context_contract.h"
#include "../../native/src/rendering/text/measurement_provider.h"
#include <cassert>
#include <iostream>
using namespace aether::rendering::text;
int main(){
 int n=0;auto ck=[&](bool v){assert(v);++n;};
 ShapingContext c{true,"TEST_ONLY", "en", "ltr", "default",400};
 ck(inspect_context(c,false)==ContextReadiness::unknown);
 ck(inspect_context({},true)==ContextReadiness::unknown);
 ck(inspect_context(c,true)==ContextReadiness::contract_complete);
 // Spy checks source/context only, not glyph counts, kerning or ligatures.
 int calls=0;auto spy=[&](const ContextGroup& g){++calls;return g.text;};
 for(auto pair:std::vector<std::pair<std::string,std::string>>{{"A","V"},{"T","o"},{"f","fi"},{" "," "},{"",""},{"x "," y"}}){
  auto whole=prepare_context_group({{pair.first+pair.second,c}});
  auto split=prepare_context_group({{pair.first,c},{pair.second,c}});
  ck(spy(whole)==spy(split));ck(split.offsets==std::vector<size_t>({0,pair.first.size()}));
 }
 ck(calls==12);
 auto rejects=[&](ShapingContext d){bool bad=false;try{prepare_context_group({{"A",c},{"V",d}});}catch(const std::invalid_argument&){bad=true;}ck(bad);};
 auto d=c;d.family="other";rejects(d);d=c;d.weight=700;rejects(d);d=c;d.italic=true;rejects(d);d=c;d.language="fr";rejects(d);d=c;d.features="kern=0";rejects(d);d=c;d.direction="rtl";rejects(d);d=c;d.vertical=true;rejects(d);d=c;d.fallback=true;rejects(d);d=c;d.variations=true;rejects(d);d=c;d.complete=false;rejects(d);
 bool bad=false;try{prepare_context_group({{"\n",c,true}});}catch(const std::invalid_argument&){bad=true;}ck(bad);
 std::cout<<"T1.5 context preparation PASS checks="<<n<<"; spy only; real shaping/PNG/Chromium NOT_RUN\n";
}
