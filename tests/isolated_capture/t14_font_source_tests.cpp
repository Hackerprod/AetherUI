#include "../../native/src/rendering/text/shaped_run.h"
#include <iostream>
#include <sstream>
#include <thread>
using namespace aether::rendering::text;
static int checks=0;
static void check(bool b){++checks;if(!b)throw std::runtime_error("assertion");}
template<class F> void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught);}
int main(){
 check(detail::sha256("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
 check(detail::sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
 check(detail::sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")=="248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
 check(detail::sha256(std::string(1000000,'a'))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
 std::istringstream in("abc");auto source=FontSource::read_exact(in,3,"test-only synthetic bytes, not a font");
 check(source->digest==detail::sha256("abc"));
 auto f=std::make_shared<const FontInstance>(source,0,64,0);
 auto f2=std::make_shared<const FontInstance>(source,0,128,0);
 check(f->source==f2->source);check(&f->bytes==&f2->bytes);check(&f->source_hash==&source->digest);
 FontInstance copied=*f;source.reset();f.reset();check(copied.source_hash==detail::sha256("abc"));
 std::thread t([f2]{check(f2->bytes.size()==3);});t.join();
 FontInstance old({97,98,99},"fake digest",0,64,0);check(old.source_hash==detail::sha256("abc"));check(old.source->provenance=="fake digest");
 FontSource corrupt({97,98,100});check(corrupt.digest!=old.source_hash);
 rejects([]{FontSource s({});});rejects([]{FontSource s(std::vector<std::uint8_t>(max_font_bytes+1));});
 rejects([]{std::istringstream i("ab");FontSource::read_exact(i,3);});
 rejects([]{std::istringstream i("abcd");FontSource::read_exact(i,3);});
 rejects([]{std::istringstream i("abc");FontSource::read_exact(i,0);});
 rejects([]{std::istringstream i("abc");FontSource::read_exact(i,max_font_bytes+1);});
 rejects([]{FontInstance f(std::shared_ptr<const FontSource>{},0,64,0);});
 ShapedRun spaces(f2,"   ",{}, {3,2,1});check(spaces.glyphs.empty());
 rejects([&]{ShapedRun invalid(f2,"x",{}, {3,2,1});});
 rejects([&]{ShapedRun invalid(f2,"",{}, {0,0,0});});
 std::cout<<"T1.4 font source: "<<checks<<" checks PASS; synthetic bytes, no real shaping\n";
}
