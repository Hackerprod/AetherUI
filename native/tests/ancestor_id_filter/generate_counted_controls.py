#!/usr/bin/env python3
"""External copies only; their clocks are not performance evidence."""
import pathlib,sys
here=pathlib.Path(__file__).resolve().parent;out=pathlib.Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=False)
common='''
#include "counters.h"
'''
helper='''
static void dump_counts(const char* phase,int iteration) {
 const auto& c=GET_COUNTS;
 std::cout<<"counted="<<phase<<",iteration="<<iteration<<",enabled="<<c.enabled_entries<<",disabled="<<c.disabled_entries<<",admitted="<<c.admitted<<",rhs="<<c.rhs<<",continuations="<<c.continuations<<",ancestor_links="<<c.ancestor_links<<",collections="<<c.collections<<",collection_links="<<c.collection_links<<",copies="<<c.copies<<'\\n';
}
'''
s=(here/'public_control.cpp').read_text().replace('#include "../test_flexbox_page.cpp"','#include "'+str(here.parent/'test_flexbox_page.cpp')+'"')
s=s.replace('int main(int argc, char** argv) {',common+'''extern "C" AncestorCounters* aether_ancestor_test_counters();
extern "C" void aether_ancestor_test_reset();
'''+helper.replace('GET_COUNTS','*aether_ancestor_test_counters()')+'\nint main(int argc, char** argv) {')
s=s.replace('        p.update_count=aether_style_runtime_resolve_subtree(p.runtime,root,&p.updates);','        aether_ancestor_test_reset();\n        p.update_count=aether_style_runtime_resolve_subtree(p.runtime,root,&p.updates);',1)
s=s.replace('        require(p.update_count>0,"initial resolution");','        dump_counts("initial",-1);\n        require(p.update_count>0,"initial resolution");')
s=s.replace('            auto start=Clock::now();','            aether_ancestor_test_reset();\n            auto start=Clock::now();')
s=s.replace('            auto end=Clock::now();','            auto end=Clock::now();\n            dump_counts("resolution",i);')
(out/'public_control.cpp').write_text(s)
s=(here/'private_control.cpp').read_text().replace('int main(int argc,char** argv){',common+helper.replace('GET_COUNTS','ancestor_counters')+'\nint main(int argc,char** argv){')
s=s.replace('need(!operation(negative),"initial negative");need(operation(positive),"positive sanity");','ancestor_counters={}; need(!operation(negative),"initial negative"); dump_counts("initial",-1); ancestor_counters={};need(operation(positive),"positive sanity");dump_counts("sanity",-1);')
s=s.replace('   auto start=Clock::now();','   ancestor_counters={}; auto start=Clock::now();')
s=s.replace('auto end=Clock::now();','auto end=Clock::now();dump_counts("negative",i);')
(out/'private_control.cpp').write_text(s)
