// Diagnostic only: original page harness, unchanged, plus scalar matcher counts.
// Link exclusively to the diagnostic shared core made by generate.py/link_core.py.
// All reported durations are instrumented and must not be used as A/B samples.
#include "counters.h"
#define main original_page_main
#include "../test_flexbox_page.cpp"
#undef main
int main(int argc,char** argv) {
    aether_ancestor_test_reset();
    const int status=original_page_main(argc,argv);
    const auto& c=*aether_ancestor_test_counters();
    std::printf("ancestor_counts enabled=%zu disabled=%zu admitted=%zu rhs=%zu searches=%zu original_links=%zu collections=%zu collection_links=%zu copies=%zu\n",
        c.enabled_entries,c.disabled_entries,c.admitted,c.rhs,c.continuations,c.ancestor_links,c.collections,c.collection_links,c.copies);
    return status;
}
