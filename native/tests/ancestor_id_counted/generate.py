#!/usr/bin/env python3
"""Generate an instrumented diagnostic copy; never edit production source.
Usage: generate.py path/to/native/src/style_runtime/selector_matcher.cpp output.cpp
Compile output with original style_runtime include path and this counters.h path.
The counters are scalar, allocation-free and must never be used for A/B times.
"""
import pathlib,sys
source=pathlib.Path(sys.argv[1]).read_text()
def replace(old,new):
    global source
    assert source.count(old)==1,(old,source.count(old))
    source=source.replace(old,new)
replace('#include "style_runtime_types.h"', '''#include "style_runtime_types.h"
#include "counters.h"
AncestorCounters ancestor_counters;
extern "C" AncestorCounters* aether_ancestor_test_counters(){return &ancestor_counters;}
extern "C" void aether_ancestor_test_reset(){ancestor_counters={};}
static const compound_selector_s* counted_rhs=nullptr;
static bool counted_query=false;
struct CountedQuery {
    explicit CountedQuery(const compound_selector_s* rhs) { counted_rhs=rhs; counted_query=true; }
    ~CountedQuery(){counted_rhs=nullptr;counted_query=false;}
};''')
replace('''                             selector_match_context_s* context) {
    if (!compound.type_name.empty())''','''                             selector_match_context_s* context) {
    if(counted_query && counted_rhs==&compound) ++ancestor_counters.rhs;
    if (!compound.type_name.empty())''')
replace('''    while (node) {
        if (match_step(sel, step_index, node, relative_anchor, context))''','''    if(counted_query) ++ancestor_counters.continuations;
    while (node) {
        if(counted_query) ++ancestor_counters.ancestor_links;
        if (match_step(sel, step_index, node, relative_anchor, context))''')
replace('''void selector_match_context_s::collect_ancestor_ids(aether_node_s* subject) {''','''void selector_match_context_s::collect_ancestor_ids(aether_node_s* subject) {
    ++ancestor_counters.collections;''')
replace('''        ++links;
        if (!is_selector_dom_element(parent)) continue;''','''        ++links;
        ++ancestor_counters.collection_links;
        if (!is_selector_dom_element(parent)) continue;''')
replace('''            ids.emplace_back(id);''','''            ids.emplace_back(id);
            ++ancestor_counters.copies;''')
replace('''    if (context.ancestor_ids_enabled_ && admits_ancestor_id_filter(sel, element)) {''','''    if(context.ancestor_ids_enabled_) ++ancestor_counters.enabled_entries;
    else ++ancestor_counters.disabled_entries;
    if (context.ancestor_ids_enabled_ && admits_ancestor_id_filter(sel, element)) {
        ++ancestor_counters.admitted;
        CountedQuery counted(&sel.steps[0].compound);''')
pathlib.Path(sys.argv[2]).write_text(source)
