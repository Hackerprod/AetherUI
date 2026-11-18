#pragma once
#include "../../layout/b2_inline_ledger.h"
#include "b2_inline_transport.h"
#include "../text/font_source.h"
#include <aether/aether.h>
#include <memory>
#include <span>
#include "../../layout/b3_width_evidence.h"
struct aether_layout_result_s;
struct aether_layout_box_s;
namespace aether::rendering::html_ltr {
#ifdef AETHER_HTML_LTR_TESTING
namespace testing {void b2_inspect(aether::layout::b2::Ledger&);void b2_result_inspect(B2InlineResult&);}
#endif
class B2Provenance final {
 struct State;
 std::unique_ptr<State> state_;
public:
 B2Provenance(aether_doc_t,aether_style_map_t,std::shared_ptr<const text::FontSource>,std::shared_ptr<const text::FontSource>,bool b3=false);
 ~B2Provenance();
 B2Provenance(const B2Provenance&)=delete;
 void valid()const;
 std::pmr::memory_resource* resource();
 aether::layout::b2::Counters& counters();
 const aether::layout::b2::Ledger& ledger()const;
 bool begin_collection(const B2InlineResult&);
 void record_measurement(std::shared_ptr<const text::ShapedRun>,aether_node_s*,const void* style);
 void work(B2Work,size_t);
 void preflight(const B2InlineResult&,size_t tokens,size_t normalized_visible);
 void inline_result(const B2InlineResult&);
 void b3_begin_result(aether_layout_result_s*,uint64_t);
 void b3_box(aether_layout_result_s*,aether_layout_box_s*,uint64_t,uint64_t,uint64_t);
 void b3_complete(aether_layout_box_s*);
 void b3_replace(aether_layout_result_s*,aether_layout_box_s*);
 void b3_offset(aether_layout_box_s*,float,float);
 void b3_discard(aether_layout_result_s*,aether_layout_box_s*,const B2InlineResult&,std::span<aether_node_s* const>);
 void b3_width(aether_layout_result_s*,aether_layout_box_s*,aether_layout_box_s*,float,float,B3WidthReason,bool,bool);
 void validate_final(aether_layout_result_s*);
};
}
