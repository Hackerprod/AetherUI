#include "intrinsic_sizing.h"
#include "flex_resolver.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <utility>

static int checks = 0;
static void require(bool value, const char* description) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", description); std::exit(1); }
}
template<class F> static void overflow(F&& f) {
    bool failed = false;
    try { f(); } catch (const std::overflow_error&) { failed = true; }
    require(failed, "non-finite value must fail before clamp/floor");
}
static float select(float c, float b, bool definite, float grow, float shrink, float h) {
    return aether_intrinsic_flex_needs_hypothetical(c,b,definite,grow,shrink) ? h : c;
}
int main() {
    const float tiny = std::numeric_limits<float>::denorm_min();
    for (const auto factors : {std::pair<float,float>{0,1}, {1,0}, {0,0}, {1,1}}) {
        require(select(40,60,true,factors.first,factors.second,60) == (factors.second == 0 ? 60 : 40), "min-content branch");
        require(select(100,60,true,factors.first,factors.second,60) == (factors.first == 0 ? 60 : 100), "max-content branch");
        require(select(60,60,true,factors.first,factors.second,90) == 60, "equal base retains contribution");
        require(select(100,NAN,false,factors.first,factors.second,60) == 100, "indefinite base retains contribution");
    }
    require(select(100,60,true,tiny,1,60) == 100, "tiny grow is not zero");
    require(select(40,60,true,1,tiny,60) == 40, "tiny shrink is not zero");
    require(select(std::nextafter(60.f,INFINITY),60,true,0,1,80) == 80, "strict growing comparison");
    require(select(std::nextafter(60.f,0.f),60,true,1,0,80) == 80, "strict shrinking comparison");
    for (const float invalid : {NAN, INFINITY, -INFINITY}) {
        overflow([&] { select(invalid,60,true,0,0,80); });
        overflow([&] { select(40,invalid,true,0,0,80); });
        overflow([&] { select(40,60,true,invalid,0,80); });
        overflow([&] { select(40,60,true,0,invalid,80); });
    }
    aether_flex_item_input_s item{};
    item.min_main_size = 70;
    item.max_main_size = 80;
    require(aether_flex_resolve_hypothetical_size(50,item) == 70, "explicit minimum");
    item.max_main_size = 40;
    require(aether_flex_resolve_hypothetical_size(50,item) == 70, "minimum wins over maximum");
    item.min_main_size = 0;
    item.main_decoration_size = 24;
    require(aether_flex_resolve_hypothetical_size(0,item) == 24, "decoration floor");
    item.main_decoration_size = 0;
    item.min_main_size = NAN;
    item.max_main_size = NAN;
    item.automatic_minimum = {false, 100, 70, NAN, 60};
    require(aether_flex_resolve_hypothetical_size(20,item) == 60, "raw auto minimum capped by suggestions");
    item.automatic_minimum.is_scroll_container = true;
    require(aether_flex_resolve_hypothetical_size(20,item) == 20, "scrollable auto minimum");
    item.max_main_size = 10;
    for (float invalid : {NAN, INFINITY, -INFINITY})
        overflow([&] { aether_flex_resolve_hypothetical_size(invalid,item); });
    item.automatic_minimum.content_size_suggestion = INFINITY;
    overflow([&] { aether_flex_resolve_hypothetical_size(20,item); });
    item.automatic_minimum.content_size_suggestion = 0;
    item.main_decoration_size = INFINITY;
    overflow([&] { aether_flex_resolve_hypothetical_size(20,item); });

    for (auto mode : {aether_intrinsic_size_mode_e::min_content, aether_intrinsic_size_mode_e::max_content}) {
        const auto sum = [&](std::vector<float> c,float gap=0,bool col=false,bool wrap=false) {
            return aether_intrinsic_flex_main_contribution(c,col,wrap,gap,mode);
        };
        require(sum({-30,50}) == 20, "signed sum");
        require(sum({-30,50},10) == 30, "gap follows signed sum");
        require(sum({-100,50},60) == 10, "gap offsets negative total before floor");
        require(sum({-100,50}) == 0, "negative total floor");
        require(sum({-30,-30}) == 0, "all negative floor");
        require(sum({-30}) == 0, "singleton floor");
        require(sum({},60) == 0, "empty has no gap");
        require(sum({-30,50},-10) == 20, "negative gap zero");
        require(sum({-30,50},0,true) == 50, "column unchanged");
        require(sum({-30,50},0,false,true) ==
            (mode == aether_intrinsic_size_mode_e::min_content ? 50 : 20),
            "wrap minimum remains widest; maximum uses a signed sum with explicit separate floor");
        const float max = std::numeric_limits<float>::max();
        require(sum({max,max,-max}) == max, "double avoids intermediate float overflow");
        require(sum({-max,-max}) == 0, "finite negative double sum floors at publication");
        overflow([&] { sum({max,max}); });
        overflow([&] { sum({max}, max); /* singleton gap absent */ sum({max,max},max); });
        for (float invalid : {NAN, INFINITY, -INFINITY}) {
            overflow([&] { sum({invalid,10}); });
            overflow([&] { sum({10},invalid); });
            overflow([&] { sum({},invalid); });
        }
    }
    std::printf("%d pure checks passed\n",checks);
}
