#pragma once
#include <cstddef>
struct AncestorCounters {
    size_t enabled_entries=0, disabled_entries=0, admitted=0, rhs=0;
    size_t continuations=0, ancestor_links=0, collections=0, collection_links=0, copies=0;
};
#if defined(__GNUC__)
__attribute__((visibility("default")))
#endif
extern AncestorCounters ancestor_counters;
extern "C" {
#if defined(__GNUC__)
__attribute__((visibility("default")))
#endif
AncestorCounters* aether_ancestor_test_counters();
#if defined(__GNUC__)
__attribute__((visibility("default")))
#endif
void aether_ancestor_test_reset();
}
