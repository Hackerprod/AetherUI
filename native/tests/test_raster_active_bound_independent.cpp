#include "../src/rendering/text/raster_active_bound.h"
#include <cstdio>
#include <cmath>
#include <limits>
#include <stdexcept>
using namespace aether::rendering::text::truetype;
namespace ab=active_bound_detail;
unsigned checks=0;void need(bool b){++checks;if(!b)throw std::runtime_error("active bound mismatch");}
int main(){for(int top:{-65536,-17,0,17,65536})for(uint64_t scans:{0ULL,1ULL,63ULL,64ULL,65ULL,131072ULL})for(int k=0;k<16;++k){double lo=-double(top)+(k-4)/8.,hi=lo+(k%5)/4.;uint64_t expected=0;for(uint64_t i=0;i<scans;++i){long double sample=-static_cast<long double>(top)+(static_cast<long double>(i)+.5L)/64;expected+=sample>=lo&&sample<hi;}uint64_t ticks=0;need(ab::count(top,scans,lo,hi,[&]{++ticks;})==expected);need(ticks<=2*ab::search_bound(scans));}
for(int top:{-65536,0,65536})for(unsigned j:{0u,1u,31u,63u,127u}){double endpoint=ab::sample(j,top);for(double edge:{std::nextafter(endpoint,-INFINITY),endpoint,std::nextafter(endpoint,INFINITY)})for(bool low:{false,true}){double lo=low?edge:-double(top)-1,hi=low?-double(top)+3:edge;uint64_t expected=0;for(unsigned n=0;n<128;++n){double sample=ab::sample(n,top);expected+=sample>=lo&&sample<hi;}need(ab::count(top,128,lo,hi,[]{})==expected);}}
for(double bad:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()}){bool rejected=false;try{ab::count(0,64,bad,1.,[]{});}catch(RasterError e){rejected=e==RasterError::numeric_failure;}need(rejected);}
for(int mode=0;mode<3;++mode){bool rejected=false;try{ab::count(mode==0?65537:0,mode==1?131073:64,mode==2?2.:0.,1.,[]{});}catch(RasterError e){rejected=e==RasterError::numeric_failure;}need(rejected);}
for(unsigned at=1;at<=4;++at){unsigned visits=0;bool threw=false;try{ab::count(0,131072,0,100,[&]{if(++visits==at)throw std::runtime_error("cancel");});}catch(const std::runtime_error&){threw=true;}need(threw&&visits==at);}
printf("Independent active bound checks=%u\n",checks);}
