#include "image_exact_lattice.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace aether::rendering::software::image_exact_lattice;
struct Test {double source[4],destination[4],clipped[4],opacity;unsigned width,height;bool full;int x,y;std::uint8_t pixels[48];unsigned expected[4];};
#include "unit-vectors.inc"
int main(){
 unsigned count=0;
 for(const auto& t:tests){Context c{};assert(prepare(c,t.source,t.destination,t.clipped,t.opacity,t.width,t.height,t.full,0));unsigned out[4];sample(c,t.pixels,t.width*4,t.x,t.y,out);for(unsigned j=0;j<4;++j)assert(out[j]==t.expected[j]);assert(out[0]<=out[3]&&out[1]<=out[3]&&out[2]<=out[3]);++count;}
 const double source[]{0,0,4,3},dest[]{0,0,10,10};Context c{};
 assert(!prepare(c,source,dest,dest,1,4,3,true,1));
 assert(!prepare(c,source,dest,dest,std::nextafter(1.,0.),4,3,true,0));
 double bad[]{0,0,10,10};bad[0]=std::numeric_limits<double>::denorm_min();assert(!prepare(c,source,bad,dest,1,4,3,true,0));
 bad[0]=32768;assert(!prepare(c,source,bad,dest,1,4,3,true,0));
 bad[0]=-32768;assert(!prepare(c,source,bad,dest,1,4,3,true,0));
 bad[0]=std::numeric_limits<double>::infinity();assert(!prepare(c,source,bad,dest,1,4,3,true,0));
 bad[0]=10;assert(!prepare(c,source,bad,dest,1,4,3,true,0));
 assert(!prepare(c,source,dest,dest,1,0,3,true,0));
 assert(!prepare(c,source,dest,dest,1,16385,3,true,0));
 const double fractional[]{0.25,0,3.25,3};assert(!prepare(c,fractional,dest,dest,1,4,3,false,0));
 const double tiny[]{0,0,1./256,1./256};assert(prepare(c,source,tiny,tiny,1,4,3,true,0));
 const double below[]{0,0,std::nextafter(1./256,0.),1./256};assert(!prepare(c,source,below,below,1,4,3,true,0));
#if defined(__SIZEOF_INT128__)
 for(unsigned k: {0u,25u,76u,104u,229u,254u})for(unsigned power: {61u,95u}){
  const Wide den=Wide(1)<<power;const Wide n=Wide(k)*den+den/2;
  assert(half_up(n-1,den)==k);assert(half_up(n,den)==k+1);assert(half_up(n+1,den)==k+1);
 }
 assert(half_up(Wide(255)*(Wide(1)<<95),Wide(1)<<95)==255);
#endif
 std::printf("PASS %u independent rational pixel vectors, domain boundaries, 37 wide half-up witnesses\n",count);
}
