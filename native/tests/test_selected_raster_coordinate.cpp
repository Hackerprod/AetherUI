#include "../src/rendering/text/selected_text_product.h"
#include <cassert>
#include <limits>
#include <iostream>
int main(){using aether::rendering::text::selected_raster_coordinate;
 struct Case{double x;int q,p;unsigned phase;};
 const Case cases[]={{0,0,0,0},{1,64,1,0},{-1,-64,-1,0},{0.5,32,0,32},{-0.5,-32,-1,32},{1.0/128,1,0,1},{-1.0/128,-1,-1,63},{63.5/64,64,1,0},{-63.5/64,-64,-1,0},{28.2390625,1807,28,15},{65536,4194304,65536,0},{-65536,-4194304,-65536,0}};
 for(auto c:cases){auto r=selected_raster_coordinate(c.x);assert(r.value26_6==c.q&&r.pixel==c.p&&r.phase==c.phase);assert(r.phase<64);}
 for(double x:{65536.1,-65536.1,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){bool rejected=false;try{(void)selected_raster_coordinate(x);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
 std::cout<<"Selected raster coordinate independent cases=16\n";
}
