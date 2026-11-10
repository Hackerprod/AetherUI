#include "intrinsic_sizing.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>

static int checks;
static void require(bool ok, const char* why) {
    ++checks;
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",why); std::exit(1); }
}
template<class F> static void overflow(F&& f) {
    bool threw=false;
    try { f(); } catch(const std::overflow_error&) { threw=true; }
    require(threw,"invalid value/overflow rejected before floor");
}
static float maximum(std::vector<float> selected, float gap, float natural_minimum) {
    return aether_intrinsic_flex_main_contribution(selected,false,true,gap,
        aether_intrinsic_size_mode_e::max_content,natural_minimum);
}
static float minimum(std::vector<float> natural) {
    return aether_intrinsic_flex_main_contribution(natural,false,true,0,
        aether_intrinsic_size_mode_e::min_content);
}
int main() {
    require(minimum({40,40})==40,"natural minimum must not become H=60");
    require(maximum({60,60},0,minimum({40,40}))==120,"independent min below selected sum");
    require(maximum({50,50},0,minimum({100,200}))==200,"natural min above selected sum");
    require(maximum({50,50},100,200)==200,"natural min equals sum plus gap");
    require(maximum({50,50},150,200)==250,"gap can exceed natural floor");
    require(maximum({-50,200},100,200)==250,"signed outer size includes gap before floor");
    require(maximum({-150,-50},300,0)==100,"gap offsets negative contributions");
    require(maximum({-150,-50},300,200)==200,"floor after compensated signed sum");
    require(maximum({-150,-50},0,0)==0,"all negative floor");
    require(maximum({-150,-50},-10,40)==40,"negative gap behaves as zero");
    require(maximum({},90,0)==0,"empty excludes gaps");
    require(maximum({},90,40)==40,"explicit natural floor contract");
    require(maximum({40},90,40)==40,"singleton excludes gaps");
    require(maximum({-30},90,0)==0,"negative singleton");
    require(maximum({150.25f,150.25f},0,40)==300.5f,"fractional contributions");
    require(minimum({-30,-50})==0,"natural minimum nonnegative");
    require(minimum({})==0,"empty natural minimum");
    const float max=std::numeric_limits<float>::max();
    require(maximum({max,max,-max},0,0)==max,"double cancellation before publishing float");
    require(maximum({-max,-max,max},max,0)==max,"double negative cancellation with two gaps");
    require(maximum({-max,-max},0,40)==40,"finite negative double below floor");
    overflow([&]{maximum({max,max},0,0);});
    overflow([&]{maximum({max,max},0,max);});
    overflow([&]{maximum({max,0},max,0);});
    for(float invalid:{NAN,INFINITY,-INFINITY}) {
        overflow([&]{maximum({invalid,40},0,40);});
        overflow([&]{maximum({40,invalid},0,40);});
        overflow([&]{maximum({40},invalid,40);});
        overflow([&]{maximum({},invalid,40);});
        overflow([&]{maximum({40},0,invalid);});
        overflow([&]{maximum({},0,invalid);});
        overflow([&]{minimum({invalid,40});});
        overflow([&]{minimum({40,invalid});});
    }
    for(auto mode:{aether_intrinsic_size_mode_e::min_content,aether_intrinsic_size_mode_e::max_content}) {
        require(aether_intrinsic_flex_main_contribution({-30,50},false,false,10,mode,200)==30,
            "nowrap ignores the wrap-only minimum floor");
        require(aether_intrinsic_flex_main_contribution({-30,50},true,true,10,mode,200)==50,
            "column retains individual maximum");
    }
    std::printf("%d wrap pure checks passed\n",checks);
}
