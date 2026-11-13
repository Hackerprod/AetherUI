#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/rendering/software/software_renderer.h"
#include "../../native/src/rendering/preparation/native_command_source.h"
#include "../../native/src/rendering/display_list_types.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;
namespace s=aether::rendering::software;
static int checks=0;
#define CK(x) do { ++checks; assert(x); } while(0)
int main(){
 // Test-only contract fixture; neither these bytes nor metrics come from a font.
 auto font=std::make_shared<const text::FontInstance>(std::vector<std::uint8_t>{1},"TEST_ONLY_NOT_A_FONT",0,640,0);
 auto run=std::make_shared<const text::ShapedRun>(font,"   ",std::vector<text::Glyph>{},aether_text_metrics_t{6,10,8});
 std::weak_ptr<const text::FontInstance> weak=font;
 p::Snapshot captured;
 {
  NativePaintList list;NativeListAccess::list(list)=new aether_display_list_s;
  aether_paint_command_t cmd{};cmd.tag=AETHER_PAINT_DRAW_TEXT;
  auto& t=cmd.data.draw_text;t.text=strdup("   ");t.rect={1,1,6,10};strcpy(t.font_family,"test-only");t.font_size=10;t.font_weight=400;t.color={1,0,0,1};
  NativeListAccess::list(list)->commands.push_back(cmd);list.text_runs.push_back({0,{run,9}});
  auto result=p::snapshot_native_opacity(list);CK(std::holds_alternative<p::Snapshot>(result));
  captured=std::get<p::Snapshot>(std::move(result));
  CK(std::get<p::Text>(captured.commands[0]).native->run==run);
 }
 run.reset();font.reset();CK(!weak.expired());
 auto prepared=p::prepare(captured);CK(std::holds_alternative<p::PreparedFrame>(prepared));
 FrameStore store;CK(store.resize({16,12,1})==FrameStatus::ok);
 const std::vector<std::shared_ptr<const text::GlyphMaskA8>> no_masks;
 // Compare clear and nonzero backdrops with/without the spaces command.
 for(bool opaque:{false,true}) {
  p::Snapshot background; if(opaque)background.commands.push_back(p::Fill{{0,0,16,12},{},{.1f,.2f,.3f,1}});
  auto bp=p::prepare(background);CK(std::holds_alternative<p::PreparedFrame>(bp));
  CK(s::render_a8(store,std::get<p::PreparedFrame>(bp),no_masks).status==s::Status::ok);
  std::optional<Frame> before;CK(store.copy_confirmed(before)==FrameStatus::ok&&before);
  auto with_spaces=captured;
  if(opaque)with_spaces.commands.insert(with_spaces.commands.begin(),background.commands[0]);
  auto wp=p::prepare(with_spaces);CK(std::holds_alternative<p::PreparedFrame>(wp));
  auto result=s::render_a8(store,std::get<p::PreparedFrame>(wp),no_masks);
  CK(result.status==s::Status::ok);CK(result.glyph_uses==0);CK(result.mask_count==0);CK(result.mask_bytes==0);CK(result.mask_pixel_visits==0);
  std::optional<Frame> after;CK(store.copy_confirmed(after)==FrameStatus::ok&&after);
  CK(after->pixels==before->pixels);CK(after->sequence>before->sequence);
 }
 CK(s::render(store,std::get<p::PreparedFrame>(prepared)).status==s::Status::unsupported);
 std::cout<<"T1.4 spaces snapshot/preparation/A8: "<<checks<<" checks PASS; TEST_ONLY metrics, no shaping/rasterization\n";
}
