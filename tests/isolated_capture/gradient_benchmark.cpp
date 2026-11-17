#include "software_renderer.h"
#include <algorithm>
#include <cassert>
#include <iostream>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;
namespace s=aether::rendering::software;
int main(){
 for(int kind=0;kind<5;++kind){
  const char* name=kind==0?"affine100":kind==1?"constant100":kind==2?"rounded100":kind==3?"solid100":"slow64stops16";
  FrameStore store;assert(store.resize({400,300,1})==FrameStatus::ok);
  p::LinearGradientFill g;g.rect=g.gradient_rect={0,0,100,100};g.direction_x=1;g.direction_y=0;g.stops={{0,{1,0,0,1}},{1,{0,0,1,1}}};
  if(kind==1)g.stops[1].color=g.stops[0].color;
  if(kind==2)g.radii={12,12,12,12,12,12,12,12};
  if(kind==4){g.rect=g.gradient_rect={0,0,16,16};g.stops.clear();for(int j=0;j<64;++j)g.stops.push_back({j/63.f,{float(j%2),0,float(1-j%2),1}});}
  p::Snapshot snapshot;if(kind!=3)snapshot.commands.push_back(g);else snapshot.commands.push_back(p::Fill{{0,0,100,100},{},{1,0,0,1}});
  auto prep=p::prepare(snapshot);assert(std::holds_alternative<p::PreparedFrame>(prep));
  std::vector<std::uint64_t> preparation,composition;
  for(int i=0;i<32;++i){auto r=s::render(store,std::get<p::PreparedFrame>(prep));assert(r.status==s::Status::ok);
   std::cout<<"sample,"<<name<<','<<i<<','<<r.preparation_ns<<','<<r.composition_ns<<','<<r.pixel_budget<<'\n';
   if(i){preparation.push_back(r.preparation_ns);composition.push_back(r.composition_ns);}
  }
  std::sort(preparation.begin(),preparation.end());std::sort(composition.begin(),composition.end());
  std::cout<<"summary,"<<name<<",warm31,preparation_median_ns="<<preparation[15]<<",preparation_p95_ns="<<preparation[29]<<",composition_median_ns="<<composition[15]<<",composition_p95_ns="<<composition[29]<<'\n';
 }
}
