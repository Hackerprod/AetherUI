#pragma once
#include <aether/rendering/preparation/paint_snapshot.hpp>
namespace aether::smoke {
namespace prep=aether::rendering::preparation;
inline constexpr const char* geometry_id="geometry-v1";
inline constexpr const char* text_id="text-v1";
inline constexpr int geometry_width=64,geometry_height=48,text_width=160,text_height=48;
prep::Snapshot geometry_fixture();
prep::Text text_fixture();
struct Probe { int x,y; unsigned r,g,b; };
inline constexpr Probe geometry_probes[]={{2,2,255,255,255},{10,10,255,0,0},
 {30,22,0,0,255},{45,22,255,255,255}};
}
