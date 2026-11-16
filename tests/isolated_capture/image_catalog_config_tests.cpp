#ifdef NDEBUG
#error assertions required
#endif
#include "image_catalog.h"
#include <cassert>
#include <iostream>
using namespace capture_images;
template<class F> bool rejected(F f){try{f();return false;}catch(Error){return true;}}
int main(){
 Config c{"assets",{{"hero","checker.png"}}, {}};validate(c);assert(c.aliases.count("hero"));
 assert(root_components("/").empty());assert(root_components(".").empty());assert(root_components("/tmp/assets").size()==2);
 for(const auto& root:{"","a/../b","a/./b","a//b","a/","a\\b"})assert(rejected([&]{root_components(root);}));
 for(const auto& alias:{"","hero.png","../hero","h ero","h/ero","h\\ero"})assert(!alias_valid(alias));
 assert(!alias_valid(std::string(65,'a')));assert(alias_valid(std::string(64,'a')));
 assert(!component_valid(std::string("a\0b",3)));
 Config duplicate{"assets",{{"hero","a"},{"hero","b"}}, {}};assert(rejected([&]{validate(duplicate);}));
 Config no_root{"",{{"hero","a"}}, {}};assert(rejected([&]{validate(no_root);}));
 Config no_entries{"assets",{}, {}};assert(rejected([&]{validate(no_entries);}));
 Config nul_root{std::string("a\0b",3),{{"hero","a"}}, {}};assert(rejected([&]{validate(nul_root);}));
 Config many{"assets",{}, {}};for(size_t n=0;n<65;++n)many.entries.push_back({"a"+std::to_string(n),"b"});assert(rejected([&]{validate(many);}));
 Config ok{};validate(ok);assert(ok.aliases.empty());
 std::cout<<"image_catalog_configuration=PASS\n";
}
