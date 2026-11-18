#pragma once
#include "../../layout/b2_inline_ledger.h"
#include "b2_inline_transport.h"
#include "../text/font_source.h"
#include <aether/aether.h>
#include <memory>
struct aether_layout_result_s;
namespace aether::rendering::html_ltr {
class B2Provenance final {
 struct State;
 std::unique_ptr<State> state_;
public:
 B2Provenance(aether_doc_t,aether_style_map_t,std::shared_ptr<const text::FontSource>,std::shared_ptr<const text::FontSource>);
 ~B2Provenance();
 B2Provenance(const B2Provenance&)=delete;
 void valid()const;
 std::pmr::memory_resource* resource();
 aether::layout::b2::Counters& counters();
 const aether::layout::b2::Ledger& ledger()const;
 void work(B2Work,size_t);
 void preflight(const B2InlineResult&,size_t tokens,size_t normalized_visible);
 void inline_result(const B2InlineResult&);
 void validate_final(aether_layout_result_s*);
};
}
