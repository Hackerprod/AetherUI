#pragma once
#include "../../layout/b2_inline_ledger.h"
#include "b2_inline_transport.h"
#include "b2_translation.h"
#include "../text/font_source.h"
#include <aether/aether.h>
#include <memory>
struct aether_layout_result_s;
namespace aether::rendering::html_ltr {
#ifdef AETHER_HTML_LTR_TESTING
namespace testing {void b2_inspect(aether::layout::b2::Ledger&);void b2_result_inspect(B2InlineResult&);}
#endif
class B2Provenance final : public B2TranslationSink {
 struct State;
 std::unique_ptr<State> state_;
public:
 B2Provenance(aether_doc_t,aether_style_map_t,std::shared_ptr<const text::FontSource>,std::shared_ptr<const text::FontSource>);
 ~B2Provenance();
 B2Provenance(const B2Provenance&)=delete;
 void begin(aether_layout_result_s*) override;
 void end(aether_layout_result_s*) noexcept override;
 uint64_t operation(aether_layout_result_s*) override;
 void visit(aether_layout_result_s*) override;
 void translate(const B2Translation&) override;
 void valid()const;
 std::pmr::memory_resource* resource();
 aether::layout::b2::Counters& counters();
 const aether::layout::b2::Ledger& ledger()const;
 bool begin_collection(const B2InlineResult&);
 void record_measurement(std::shared_ptr<const text::ShapedRun>,aether_node_s*,const void* style);
 void work(B2Work,size_t);
 void preflight(const B2InlineResult&,size_t tokens,size_t normalized_visible);
 void inline_result(const B2InlineResult&);
 void validate_final(aether_layout_result_s*);
};
}
