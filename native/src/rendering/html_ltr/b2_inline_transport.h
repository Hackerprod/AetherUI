#pragma once
#include "../text/shaped_run.h"
#include <memory_resource>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <limits>
struct aether_node_s;
struct aether_layout_box_s;
namespace aether::rendering::html_ltr {
inline constexpr size_t b2_no_index=std::numeric_limits<size_t>::max();
enum class B2Work {token_visits, tokens, fragments,lines,placements,glyphs};
enum class B2Disposition {visible, contributor, leading, trailing, soft_break, empty_context, flex_whitespace};
struct B2Participant {aether_node_s* node; aether_node_s* owner; const void* style;};
struct B2Range {aether_node_s* node;size_t begin,end;B2Disposition disposition;size_t placement=b2_no_index,group=b2_no_index,line=b2_no_index;
 // Canonical collector IDs survive final disposition changes (including soft discard).
 size_t token=b2_no_index,fragment=b2_no_index;};
enum class B2LineEnd {soft,forced,paragraph};
struct B2Line {float baseline,x,width,height,y; B2LineEnd end;};
struct B2Placement {std::shared_ptr<const text::ShapedRun> run; aether_node_s* owner;const void* style;size_t token,fragment,group,line;float x,y,width,height,baseline; aether_layout_box_s* box=nullptr;uint64_t box_id=0;};
struct B2Break {aether_node_s* node;size_t line;};
struct B2SoftBreak {size_t group,line;std::shared_ptr<const text::ShapedRun> space;};
// Ephemeral immutable view delivered synchronously after the common resolver's
// final geometry pass. Indexes are local to this context, not measurement IDs.
struct B2InlineResult {
 union { aether_layout_box_s* anchor=nullptr; aether_layout_box_s* anchor_box; };
 uint64_t result_id=0,generation_id=0,anchor_box_id=0;
 aether_node_s* block_owner=nullptr;
 float start_x=0,start_y=0,available_width=0,minimum_line_height=0;
 std::pmr::vector<B2Participant> participants;
 std::pmr::vector<B2Range> ranges;
 std::pmr::vector<B2Line> lines;
 std::pmr::vector<B2Placement> placements;
 std::pmr::vector<B2Break> breaks;
 std::pmr::vector<B2SoftBreak> soft_breaks;
 explicit B2InlineResult(std::pmr::memory_resource* r):participants(r),ranges(r),lines(r),placements(r),breaks(r),soft_breaks(r){}
};
}
