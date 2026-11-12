#include "fixture.h"
namespace aether::smoke {
prep::Snapshot geometry_fixture() {
 return {{prep::Fill{{6,6,14,14},{},{1,0,0,1}},
  prep::PushClip{{24,16,16,16},{}},prep::PushTransform{{1,0,0,1,20,10}},
  prep::Fill{{0,0,30,30},{},{0,0,1,1}},prep::PopTransform{},prep::PopClip{}}};
}
prep::Text text_fixture() {return {"Aether 123",{8,8,144,32},"sans-serif",16,400,
 AETHER_TEXT_ALIGN_LEFT,{0,0,0,1},AETHER_FALSE};}
}
