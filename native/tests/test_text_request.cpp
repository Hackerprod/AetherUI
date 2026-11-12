#ifdef NDEBUG
#error "Assertions required"
#endif
#include "../src/rendering/text/text_request.h"
#include <cassert>
#include <limits>
#include <utility>
#include <cstdlib>
#include <new>
namespace { bool fail_next_allocation=false; }
void* operator new(std::size_t n) {
 if(fail_next_allocation) {fail_next_allocation=false;throw std::bad_alloc();}
 if(void* p=std::malloc(n?n:1)) return p;
 throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
using namespace aether::rendering;
namespace tx=text;
namespace pp=preparation;
const tx::Request& value(const tx::Result& r) {assert(std::holds_alternative<tx::Request>(r));return std::get<tx::Request>(r);}
int main() {
 pp::Text t{"hello",{1,2,3,4},"sans",12,400,AETHER_TEXT_ALIGN_START,{0,0,0,1},AETHER_FALSE};
 auto r=tx::paint(t);assert(value(r).text()=="hello" && !value(r).used_line_height());
 t.text="changed";t.font_family="serif";assert(value(r).text()=="hello" && value(r).family()=="sans");
 t.text="hello";t.font_family="sans";
 auto other=t;other.rect.x=100;other.rect.y=200;other.color={1,0,0,1};assert(value(tx::paint(other))==value(r));
 for(int field=0;field<8;++field) {
  other=t;
  switch(field) {
   case 0:other.text="other";break;case 1:other.font_family="Sans";break;
   case 2:other.font_size=13;break;case 3:other.font_weight=500;break;
   case 4:other.wrap=AETHER_TRUE;break;case 5:other.rect.width=5;break;
   case 6:other.rect.height=6;break;case 7:other.text_align=AETHER_TEXT_ALIGN_END;break;
  }
  assert(!(value(tx::paint(other))==value(r)));
 }
 tx::IntrinsicInput in{"hello","sans",12,400,14,std::nullopt};
 auto intrinsic=tx::intrinsic(in);assert(value(intrinsic).purpose()==tx::Purpose::intrinsic);
 assert(!value(intrinsic).wrap()&&!value(intrinsic).width()&&!value(intrinsic).height()&&!value(intrinsic).alignment());
 assert(!(value(intrinsic)==value(r)));
 in.max_width=0;assert(!(value(tx::intrinsic(in))==value(intrinsic)));
 in.max_width=std::nullopt;in.used_line_height=15;assert(!(value(tx::intrinsic(in))==value(intrinsic)));
 std::string exact=" A\n\r\t ";exact+="\xd7\x90";exact+="e\xcc\x81 ";
 in.text=exact;auto bytes=tx::intrinsic(in);assert(value(bytes).text()==exact);
 exact.clear();assert(!value(bytes).text().empty());
 auto owned=[] {std::string temporary="owned",family="font";return tx::intrinsic({temporary,family,12,400,0,{}});}();
 assert(value(owned).text()=="owned");
 tx::Request copied{value(owned)};assert(copied==value(owned));
 auto empty=tx::intrinsic({"","font",12,400,0,0});assert(value(empty).text().empty()&&value(empty).width()==0);
 in.text="hello"; // restore a live valid view before testing other fields
 for(float invalid:{-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
  auto bad=in;bad.max_width=invalid;assert(std::get<pp::ErrorCode>(tx::intrinsic(bad))==pp::ErrorCode::INVALID_PAYLOAD);
  bad=in;bad.used_line_height=invalid;assert(std::get<pp::ErrorCode>(tx::intrinsic(bad))==pp::ErrorCode::INVALID_PAYLOAD);
 }
 auto bad=in;bad.font_size=0;assert(std::get<pp::ErrorCode>(tx::intrinsic(bad))==pp::ErrorCode::INVALID_PAYLOAD);
 // Passing an rvalue to a by-value consumer invokes the copy-only constructor.
 auto consume=[](tx::Request request) {return request.text();};
 assert(consume(std::move(std::get<tx::Request>(owned)))=="owned");
 assert(value(owned).text()=="owned");
 std::string boundary_text(pp::max_text_bytes,'x'),boundary_family(63,'f');
 auto boundary=tx::intrinsic({boundary_text,boundary_family,12,400,0,{}});
 assert(value(boundary).text().size()==pp::max_text_bytes && value(boundary).family().size()==63);
 auto bad_family=in;bad_family.family="";
 assert(std::get<pp::ErrorCode>(tx::intrinsic(bad_family))==pp::ErrorCode::INVALID_PAYLOAD);
 std::string oversized_family(64,'f');bad_family.family=oversized_family;
 assert(std::get<pp::ErrorCode>(tx::intrinsic(bad_family))==pp::ErrorCode::LIMIT_EXCEEDED);
 std::string longtext(pp::max_text_bytes+1,'x');bad=in;bad.text=longtext;
 assert(std::get<pp::ErrorCode>(tx::intrinsic(bad))==pp::ErrorCode::LIMIT_EXCEEDED);
 std::string nul("a\0b",3);bad=in;bad.text=nul;
 assert(std::get<pp::ErrorCode>(tx::intrinsic(bad))==pp::ErrorCode::INVALID_PAYLOAD);
 bad=in;bad.text="\xc0\x80";assert(std::get<pp::ErrorCode>(tx::intrinsic(bad))==pp::ErrorCode::INVALID_UTF8);
 for(int kind=0;kind<7;++kind) {
  other=t;
  switch(kind) {
   case 0:other.text=longtext;break;case 1:other.text=nul;break;
   case 2:other.text="\xc0\x80";break;case 3:other.color.r=2;break;
   case 4:other.rect.x=std::numeric_limits<float>::infinity();break;
   case 5:other.font_family=std::string(64,'a');break;case 6:other.wrap=2;break;
  }
  std::size_t a=7,b=7;
  auto direct=pp::validate_text(other,a),through=pp::validate_command(pp::Command{other},b);
  assert(direct==through&&a==7&&b==7);
  assert(std::get<pp::ErrorCode>(tx::paint(other))==*direct);
 }
 std::size_t a=0,b=0;
 assert(!pp::validate_text(t,a)&&!pp::validate_command(pp::Command{t},b)&&a==b&&a==9);
 std::string allocating(100,'x');
 tx::IntrinsicInput oom{allocating,"font",12,400,14,{}};
 fail_next_allocation=true;
 auto no_memory=tx::intrinsic(oom);
 assert(std::get<pp::ErrorCode>(no_memory)==pp::ErrorCode::OUT_OF_MEMORY);
 assert(allocating.size()==100 && value(owned).text()=="owned");
 other=t;other.text=allocating;
 fail_next_allocation=true;
 auto paint_oom=tx::paint(other);
 assert(std::get<pp::ErrorCode>(paint_oom)==pp::ErrorCode::OUT_OF_MEMORY);
 a=pp::max_string_bytes;assert(pp::validate_text(t,a)==pp::ErrorCode::LIMIT_EXCEEDED&&a==pp::max_string_bytes);
}
