#ifdef NDEBUG
#error Assertions required
#endif
#include "atlas_io.h"
#include <cassert>
#include <iostream>
using namespace q1b_atlas;
int main(){
 const auto root=fs::temp_directory_path()/("q1b-stage-test-"+std::to_string(getpid()));require(fs::create_directory(root),"test_dir_exists");
 try{
  const Bytes marker{1,2,3,4};fs::create_directory(root/"previous");write(root/"previous"/"confirmed",marker);
  for(unsigned fault=0;fault<6;++fault){const auto next=root/("run-"+std::to_string(fault));
   try{Stage stage(next);write(stage.path()/"report.log",marker);if(fault==0)throw std::bad_alloc();throw std::runtime_error(fault==1?"cancel":fault==2?"budget":fault==3?"encode":fault==4?"verify":"commit");}catch(const std::exception&){}
   assert(!fs::exists(next));assert(read(root/"previous"/"confirmed",100)==marker);
  }
  {Stage stage(root/"success");write(stage.path()/"report.log",marker);stage.commit();}
  assert(read(root/"success"/"report.log",100)==marker);
  bool refused=false;try{Stage existing(root/"success");}catch(const std::exception&){refused=true;}assert(refused);
  {Stage stage(root/"race");fs::create_directory(root/"race");bool stopped=false;try{stage.commit();}catch(const std::exception&){stopped=true;}assert(stopped&&fs::is_directory(root/"race"));}
  Bytes pixels(1024*1024*4,0);auto png=smoke_png::png(pixels,1024,1024);verify_png(png,pixels);
  for(std::size_t at:{0U,16U,33U,1000U}){auto bad=png;bad[at]^=1;bool failed=false;try{verify_png(bad);}catch(const std::exception&){failed=true;}assert(failed);}
  for(std::size_t n:{0U,7U,12U,33U,1000U}){bool failed=false;try{verify_png(std::span(png).first(n));}catch(const std::exception&){failed=true;}assert(failed);}
  for(auto& p:fs::directory_iterator(root))assert(p.path().filename().string().find(".partial-")!=0);
  fs::remove_all(root);std::cout<<"Q1b atlas staging PASS failure_categories=6 no_replace=2 png_corruption=4 png_truncation=5\n";
 }catch(...){fs::remove_all(root);throw;}
}
