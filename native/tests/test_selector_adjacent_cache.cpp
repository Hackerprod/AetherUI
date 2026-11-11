// Scoped adjacent cache: independent structural oracle, recursive differential,
// activation gates and allocator fault sweep. Checks intentionally survive NDEBUG.
#include "style_runtime_types.h"
#include "selector_match_context.h"
#include "../src/dom/dom_types.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <random>
#include <type_traits>
#include <vector>

namespace {
bool track = false;
size_t allocations = 0, fail_at = std::numeric_limits<size_t>::max();
size_t live_bytes = 0, peak_bytes = 0, total_bytes = 0;
struct alignas(std::max_align_t) AllocationHeader { size_t bytes; bool counted; };
}
void* operator new(size_t n) {
    if (track && allocations++ == fail_at) throw std::bad_alloc();
    if (n > std::numeric_limits<size_t>::max() - sizeof(AllocationHeader)) throw std::bad_alloc();
    auto* h = static_cast<AllocationHeader*>(std::malloc(sizeof(AllocationHeader) + (n ? n : 1)));
    if (!h) throw std::bad_alloc();
    h->bytes = n; h->counted = track;
    if (track) { live_bytes += n; total_bytes += n; peak_bytes = std::max(peak_bytes, live_bytes); }
    return h + 1;
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept {
    if (!p) return;
    auto* h = static_cast<AllocationHeader*>(p) - 1;
    if (h->counted) live_bytes -= h->bytes;
    std::free(h);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, size_t) noexcept { ::operator delete(p); }

struct selector_match_context_test_access {
    static const auto& parents(const selector_match_context_s& c) {
        static const std::unordered_map<aether_node_s*, selector_match_context_s::previous_parent_s> empty;
        return c.previous_parents_ ? *c.previous_parents_ : empty;
    }
    static bool nth_empty(const selector_match_context_s& c) { return !c.parents_; }
};
namespace {
size_t checks = 0, failures = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value && ++failures <= 30) std::fprintf(stderr, "FAIL: %s\n", label);
}
bool eligible(const aether_node_s* n) {
    return n && n->type == AETHER_NODE_ELEMENT && !n->is_generated && !n->anonymous_flex_owner;
}
struct Scan { aether_node_s* previous = nullptr; size_t visited = 0, ordinal = 0; };
Scan scan(aether_node_s* n) {
    Scan result;
    if (!eligible(n) || !n->parent || n->parent->is_generated || n->parent->anonymous_flex_owner) return result;
    aether_node_s* previous = nullptr;
    size_t ordinal = 0;
    for (auto* child : n->parent->children) {
        ++result.visited;
        if (!eligible(child)) continue;
        ++ordinal;
        if (child == n) { result.previous = previous; result.ordinal = ordinal; return result; }
        previous = child;
    }
    return result;
}
aether_node_s* oracle(aether_node_s* n) { return scan(n).previous; }
struct Fixture {
    aether_node_s parent;
    std::vector<std::unique_ptr<aether_node_s>> nodes;
    explicit Fixture(size_t count) {
        parent.type = AETHER_NODE_ELEMENT; parent.tag_name = "section";
        for (size_t i = 0; i < count; ++i) {
            auto n = std::make_unique<aether_node_s>();
            n->type = AETHER_NODE_ELEMENT; n->tag_name = "div"; n->parent = &parent;
            n->attributes["class"] = "item";
            n->attributes["id"] = "duplicate-id";
            parent.children.push_back(n.get()); nodes.push_back(std::move(n));
        }
    }
};
const auto& parents(const selector_match_context_s& c) { return selector_match_context_test_access::parents(c); }
void audit(const selector_match_context_s& c) {
    for (const auto& [p,e] : parents(c)) {
        for (const auto& [n,prev] : e.previous) check(prev == oracle(n), "every memo has exact predecessor identity");
        if (e.complete) for (auto* n : p->children) if (eligible(n)) {
            auto it = e.previous.find(n);
            check(it != e.previous.end() && it->second == oracle(n), "complete map includes every eligible identity");
        }
    }
}
void activation() {
    static_assert(!std::is_copy_constructible_v<selector_match_context_s>);
    static_assert(!std::is_move_constructible_v<selector_match_context_s>);
    const auto maximum=std::numeric_limits<size_t>::max();size_t budget=7;
    check(aether::selector_position::work_budget(maximum/16,budget) && budget==(maximum/16)*16,"largest representable work budget");
    budget=7;check(!aether::selector_position::work_budget(maximum/16+1,budget) && budget==7,"overflow refuses budget without changing output");
    check(aether::selector_position::next_ordinal(maximum-1)==maximum,"last representable ordinal");
    bool overflow=false;try{(void)aether::selector_position::next_ordinal(maximum);}catch(const std::length_error&){overflow=true;}
    check(overflow,"ordinal overflow throws rather than wraps");
    for (size_t count : {0,1,32,127,128,129,256}) {
        Fixture f(count); selector_match_context_s c;
        allocations = 0; track = true;
        for (size_t i = 0; i < std::min(count,size_t(128)); ++i)
            check(c.previous_sibling(f.nodes[i].get()) == (i ? f.nodes[i-1].get() : nullptr), "cheap prefix identity");
        track = false;
        check(allocations == 0 && parents(c).empty(), "small parents and cheap scans allocate nothing");
    }
    Fixture f(65536); selector_match_context_s c;
    allocations = 0; track = true;
    for (int i=0;i<8;++i) check(c.previous_sibling(f.nodes[32].get()) == f.nodes[31].get(), "cheap huge parent");
    track = false; check(allocations == 0 && parents(c).empty(), "repeated cheap scan has no metadata");
    for (int i=0;i<8;++i) check(c.previous_sibling(f.nodes.back().get()) == f.nodes[65534].get(), "tail memo hit");
    const auto& e = parents(c).at(&f.parent);
    check(e.previous.size()==1 && e.remaining==15*65536 && e.observed==65536 && !e.complete, "hits do not pay work or grow memo");
    selector_match_context_s sparse;
    for (size_t i=0;i<32;++i) {
        check(sparse.previous_sibling(f.nodes[65535-i].get()) == f.nodes[65534-i].get(), "sparse identity");
        const auto& x=parents(sparse).at(&f.parent);
        check(!x.complete && x.previous.size()==i+1, "coverage prevents sparse full map");
    }
    check(parents(sparse).at(&f.parent).remaining==0, "remaining work saturates at zero");
    Fixture prefix(4097);
    for (size_t i=0;i<4096;++i) prefix.nodes[i]->type=AETHER_NODE_TEXT;
    selector_match_context_s null_hit;
    for (int repeat=0;repeat<8;++repeat) {
        check(null_hit.previous_sibling(prefix.nodes.back().get())==nullptr, "first element memo is a null hit");
        const auto& x=parents(null_hit).at(&prefix.parent);
        check(x.previous.size()==1 && x.previous.count(prefix.nodes.back().get())==1 && x.remaining==15*4097 && x.observed==1 && !x.complete, "null hit does not rescan/pay budget");
        check(x.previous.bucket_count()<128, "text does not cause reserve(raw children)");
    }
    std::vector<std::unique_ptr<Fixture>> many;
    for(int i=0;i<128;++i) many.push_back(std::make_unique<Fixture>(256));
    selector_match_context_s multi; allocations=0; track=true;
    for(auto& p:many) check(multi.previous_sibling(p->nodes[32].get())==p->nodes[31].get(), "many cheap parents");
    track=false; check(allocations==0 && parents(multi).empty(), "many cheap parents omit metadata");
    for(auto& p:many) for(auto& n:p->nodes) check(multi.previous_sibling(n.get())==oracle(n.get()), "many dense parents");
    check(parents(multi).size()==128, "all participating parents retained");
    for(const auto& [p,x]:parents(multi)) check(x.complete && x.previous.size()==256, "many parent maps complete");
    check(selector_match_context_test_access::nth_empty(multi), "adjacent-only never allocates nth state");
    audit(multi);
}
// Reconstruct structural work from private state before/after each query. This
// checks the real activation schedule without adding counters to the hot path.
// It is a state-based work model, not a direct production visit measurement.
void trace(size_t count, int shape, int order, size_t expected=0) {
    Fixture f(count*(shape?2:1)); std::vector<aether_node_s*> queries;
    for(size_t i=0;i<f.nodes.size();++i) {
        bool text=shape==1 ? i%2==0 : shape==2 && i<count;
        if(text) f.nodes[i]->type=AETHER_NODE_TEXT; else queries.push_back(f.nodes[i].get());
    }
    if(order==1) std::reverse(queries.begin(),queries.end());
    if(order==2) { std::mt19937 rng(1501); std::shuffle(queries.begin(),queries.end(),rng); }
    if(order==3) queries.assign(8, f.nodes.back().get());
    if(order==4) queries.assign(2, f.nodes[32].get());
    if(order==5) { queries.clear(); for(size_t i=0;i<32;++i) queries.push_back(f.nodes[f.nodes.size()-1-i].get()); }
    selector_match_context_s c;
    size_t work=0, baseline=0, builds=0;
    for(auto* n:queries) {
        auto before=scan(n); baseline+=before.visited;
        auto entry=parents(c).find(&f.parent);
        const bool has=entry!=parents(c).end();
        const bool complete=has && entry->second.complete;
        const bool hit=has && (complete || entry->second.previous.count(n));
        const size_t old_remaining=has?entry->second.remaining:16*f.parent.children.size();
        const size_t old_observed=has?entry->second.observed:0;
        const size_t old_size=has?entry->second.previous.size():0;
        if(!hit) work+=before.visited;
        check(c.previous_sibling(n)==before.previous,"trace exact predecessor");
        entry=parents(c).find(&f.parent);
        if(entry!=parents(c).end()) {
            const auto& e=entry->second;
            if(!complete && e.complete) { work+=f.parent.children.size(); ++builds; }
            if(hit) check(e.remaining==old_remaining && e.observed==old_observed && e.previous.size()==old_size,"trace hit leaves metadata unchanged");
            else if(before.visited>128 && before.ordinal) {
                const size_t paid=before.visited>=old_remaining?0:old_remaining-before.visited;
                const size_t observed=std::max(old_observed,before.ordinal);
                const size_t needed=observed/8+(observed%8!=0);
                check(e.remaining==paid && e.observed==observed,"exact debit and max ordinal");
                check(e.complete==(paid==0 && old_size+1>=needed),"exact work AND ceiling coverage activation");
            }
        }
    }
    check(work<=baseline+baseline/16,"state-derived structural budget ceiling");
    if(expected) check(work==expected,"fixed proposal structural trace reproduced");
    check(builds<=1,"at most one full map per parent/scope");
    std::printf("trace n=%zu shape=%d order=%d baseline=%zu modeled-work=%zu builds=%zu\n",count,shape,order,baseline,work,builds);
    audit(c);
}
compiled_selector_s parse(const char* text) {
    compiled_selector_s s; check(selector_parse(text,s),text); return s;
}
void differential() {
    const char* sources[]={
        "div + div", "div + div + div", "section > div + div", "section div + div", "div + div ~ div", "div ~ div + div",
        ":nth-child(odd) + :nth-child(even)", "div + :nth-child(3n+1)", ":is(div + div)", ":where(div + div)", ":not(div + div)",
        ":is(section > div + div, div + section)", ":not(:where(div + :nth-child(3n)))",
        "section:has(div + div)", "section:has(div + div)", "div:has(div + div)", "div:has(div ~ div + div)",
        "div:has(div + div + div)", "section:has(section > div + div)", "section:has(section div + div)",
        ":is(div:has(div + div), section:has(div + div))", "div + div::before", "div + div::after",
        "div + section > div", "div + section i", "section:has(section > div + div)",
        ".a + .b + .c", ".a + .b ~ .c", ".a ~ .b + .c", ":not(.a + .b) + .c"
    };
    std::vector<compiled_selector_s> ss; for(auto* s:sources) ss.push_back(parse(s));
    std::mt19937 rng(0x41444a43);
    for(int trial=0;trial<20;++trial) {
        Fixture f(256+rng()%64), nested(170);
        nested.parent.parent=&f.parent; f.parent.children.push_back(&nested.parent);
        for(size_t i=0;i<f.nodes.size();++i) {
            auto& n=f.nodes[i]; n->attributes["class"]=i%3==0?"a":i%3==1?"b":"c";
            switch(rng()%12) {
                case 0:n->type=AETHER_NODE_TEXT;break;
                case 1:n->type=AETHER_NODE_COMMENT;break;
                case 2:n->is_generated=true;n->style_origin=&f.parent;n->pseudo_element=aether_pseudo_element_t::before;break;
                case 3:n->anonymous_flex_owner=&f.parent;break;
                default:break;
            }
        }
        for(int phase=0;phase<3;++phase) {
            {
                selector_match_context_s c;
                for(auto& s:ss) {
                    check(selector_match(s,&f.parent,c)==selector_match(s,&f.parent),"parent recursive differential");
                    for(auto& n:f.nodes) check(selector_match(s,n.get(),c)==selector_match(s,n.get()),"mixed recursive differential");
                    for(auto& n:nested.nodes) check(selector_match(s,n.get(),c)==selector_match(s,n.get()),"nested recursive differential");
                }
                for(auto& n:f.nodes) check(c.previous_sibling(n.get())==oracle(n.get()),"mixed independent structural oracle");
                audit(c);
            }
            std::shuffle(f.parent.children.begin(),f.parent.children.end(),rng);
            f.parent.type=phase%2?AETHER_NODE_ELEMENT:AETHER_NODE_DOCUMENT;
        }
    }
    // Positive expectations and expensive retained identity prove propagation,
    // where differential equality alone would permit silently dropped contexts.
    Fixture f(256), deep(256); deep.parent.parent=&f.parent; f.parent.children.push_back(&deep.parent);
    aether_node_s leaf; leaf.type=AETHER_NODE_ELEMENT; leaf.tag_name="i";
    leaf.parent=deep.nodes.front().get(); leaf.parent->children.push_back(&leaf);
    struct Path { const char* selector; aether_node_s* subject; aether_node_s* memo; };
    const Path paths[]={
        {"div + div",f.nodes.back().get(),f.nodes.back().get()},
        {"div + div + div",f.nodes.back().get(),f.nodes.back().get()},
        {":is(div + div)",f.nodes.back().get(),f.nodes.back().get()},
        {":where(div + div)",f.nodes.back().get(),f.nodes.back().get()},
        {":not(section + div)",f.nodes.back().get(),f.nodes.back().get()},
        {"section > div + div",f.nodes.back().get(),f.nodes.back().get()},
        {"section div + div",f.nodes.back().get(),f.nodes.back().get()},
        {"div:nth-child(254) + div ~ div",f.nodes.back().get(),f.nodes[254].get()},
        {"div + section > div",deep.nodes.front().get(),&deep.parent},
        {"div + section i",&leaf,&deep.parent},
        {"section:has(div:nth-child(255) + div)",&f.parent,f.nodes.back().get()},
        {"section:has(div + section)",&f.parent,&deep.parent},
        {"section:has(div ~ div:nth-child(256) + section)",&f.parent,&deep.parent},
        {"div + div::before",f.nodes.back().get(),f.nodes.back().get()}
    };
    for(const auto& path:paths) {
        auto s=parse(path.selector); selector_match_context_s c;
        check(selector_match(s,path.subject),"generic positive propagation expectation");
        check(selector_match(s,path.subject,c),"context positive propagation expectation");
        auto p=parents(c).find(path.memo->parent);
        check(p!=parents(c).end(),"context reaches recursive route");
        if(p!=parents(c).end()) check(p->second.previous.count(path.memo)==1,"exact expensive identity memoized by recursive route");
    }
    // The left edge of a :has chain must be below its subject. Here the
    // preceding div is outside deep.parent, even though i is inside it.
    for(auto* source:{"section:has(div + section i)","section:has(div + section > div)"}) {
        auto s=parse(source); selector_match_context_s c;
        check(!selector_match(s,&deep.parent),"generic anchor prevents escaping subject");
        check(!selector_match(s,&deep.parent,c),"context anchor prevents escaping subject across adjacent edge");
        check(selector_match(s,&f.parent,c),"same chain valid when left edge is below outer anchor");
        const auto entry=parents(c).find(&f.parent);
        check(entry!=parents(c).end() && entry->second.previous.count(&deep.parent),"anchor failure still forwards context through adjacent edge");
    }
    // Leading combinators remain unsupported by the existing parser.
    for(auto* source:{"section:has(> div + div)","div:has(+ div)","div:has(~ div + div)"}) {
        compiled_selector_s rejected;check(!selector_parse(source,rejected),"leading relative combinator support unchanged");
    }
    auto rejected_right=parse("div + .missing");
    {selector_match_context_s c;allocations=0;track=true;
     check(!selector_match(rejected_right,f.nodes.back().get(),c),"rightmost compound fails before navigation");
     track=false;check(allocations==0 && parents(c).empty(),"rightmost failure never activates adjacent index");}
    auto plain=parse("div"); selector_match_context_s c; allocations=0; track=true;
    for(auto& n:f.nodes) check(selector_match(plain,n.get(),c),"plain matcher");
    track=false; check(allocations==0 && parents(c).empty() && selector_match_context_test_access::nth_empty(c),"no adjacent/nth means zero heap allocation");
}
void membership_and_lifetime() {
    Fixture f(256); aether_node_s missing; missing.type=AETHER_NODE_ELEMENT; missing.parent=&f.parent;
    {
        selector_match_context_s c;
        check(c.previous_sibling(nullptr)==nullptr,"null absent");
        check(c.previous_sibling(&missing)==nullptr && parents(c).empty(),"absent subject creates no metadata");
        check(c.previous_sibling(f.nodes.back().get())==f.nodes[254].get(),"partial seed");
        const auto remaining=parents(c).at(&f.parent).remaining;
        check(c.previous_sibling(&missing)==nullptr && parents(c).at(&f.parent).remaining==remaining,"partial absent miss neither memoized nor charged");
        check(c.previous_sibling(f.nodes[199].get())==f.nodes[198].get(),"partial miss scans actual member");
        for(auto& n:f.nodes) (void)c.previous_sibling(n.get());
        check(parents(c).at(&f.parent).complete,"membership map complete");
        check(c.previous_sibling(&missing)==nullptr,"complete absent miss"); audit(c);
    }
    f.parent.children.insert(f.parent.children.begin()+5,f.nodes.front().get());
    {
        selector_match_context_s c;
        for(auto& n:f.nodes) check(c.previous_sibling(n.get())==oracle(n.get()),"duplicate pointer first occurrence preserved");
        check(parents(c).at(&f.parent).complete,"duplicate full map");
        check(c.previous_sibling(f.nodes[5].get())==f.nodes[0].get(),"duplicate occurrence becomes predecessor of next sibling"); audit(c);
        auto s=parse("div + div::before"); aether_node_s pseudo;
        pseudo.type=AETHER_NODE_ELEMENT; pseudo.is_generated=true; pseudo.pseudo_element=aether_pseudo_element_t::before; pseudo.style_origin=f.nodes[254].get();
        check(selector_match(s,&pseudo,c),"pseudo styled subject uses host predecessor");
        check(c.previous_sibling(&pseudo)==nullptr,"generated traversal subject never normalizes");
        pseudo.style_origin=&pseudo;check(!selector_match(s,&pseudo,c),"self origin rejected");
        pseudo.style_origin=nullptr;check(!selector_match(s,&pseudo,c),"null origin rejected");
        pseudo.style_origin=f.nodes[254].get();pseudo.anonymous_flex_owner=&f.parent;
        check(!selector_match(s,&pseudo,c),"anonymous subject rejected");
    }
    missing.parent=nullptr; {selector_match_context_s c;check(c.previous_sibling(&missing)==nullptr,"detached subject has no predecessor");}
    for(int kind=0;kind<3;++kind) {
        f.parent.is_generated=kind==0;f.parent.anonymous_flex_owner=kind==1?&missing:nullptr;
        f.parent.type=kind==2?AETHER_NODE_DOCUMENT:AETHER_NODE_ELEMENT;
        selector_match_context_s c;
        check(c.previous_sibling(f.nodes.back().get())==(kind==2?f.nodes[254].get():nullptr),"generated/anonymous boundary and document parent");
    }
    f.parent.type=AETHER_NODE_ELEMENT;
    alignas(aether_node_s) unsigned char storage[sizeof(aether_node_s)];
    auto* reused=new(storage) aether_node_s;reused->type=AETHER_NODE_ELEMENT;reused->parent=&f.parent;f.parent.children.push_back(reused);
    {selector_match_context_s c;check(c.previous_sibling(reused)==f.nodes.back().get(),"first lifetime expensive identity");check(parents(c).at(&f.parent).previous.count(reused)==1,"first lifetime retained");}
    f.parent.children.pop_back();reused->~aether_node_s();
    reused=new(storage) aether_node_s;reused->type=AETHER_NODE_ELEMENT;reused->parent=&f.parent;f.parent.children.insert(f.parent.children.begin(),reused);
    {selector_match_context_s c;check(c.previous_sibling(reused)==nullptr,"new lifetime same pointer fresh first-child result");}
    f.parent.children.erase(f.parent.children.begin());reused->~aether_node_s();
    Fixture other(160);auto* moved=f.nodes.back().get();f.parent.children.pop_back();moved->parent=&other.parent;other.parent.children.push_back(moved);
    {selector_match_context_s c;check(c.previous_sibling(moved)==other.nodes.back().get(),"reparent fresh scope");}
    Fixture root(0),hidden(170);hidden.parent.parent=&root.parent;root.parent.children.push_back(&hidden.parent);
    auto has=parse("section:has(div:nth-child(169) + div)");
    for(int kind=0;kind<2;++kind) {
        hidden.parent.is_generated=kind==0; hidden.parent.anonymous_flex_owner=kind==1?&root.parent:nullptr;
        selector_match_context_s c; check(!selector_match(has,&root.parent,c) && !selector_match(has,&root.parent),"has prunes whole synthetic subtree");
        check(parents(c).empty(),"synthetic subtree creates no adjacent state");
    }
    hidden.parent.is_generated=false;hidden.parent.anonymous_flex_owner=nullptr;
    {selector_match_context_s c;check(selector_match(has,&root.parent,c),"restored subtree fresh scope matches");check(!parents(c).empty(),"restored subtree context propagated");}
}
struct Memory { size_t calls,total,peak,retained; };
template<class Work> Memory measure(const char* name,Work work) {
    check(live_bytes==0,"memory interval begins empty"); allocations=0;total_bytes=0;peak_bytes=0;size_t retained=0;track=true;
    {selector_match_context_s c;work(c);retained=live_bytes;}
    track=false;check(live_bytes==0,"all scratch heap bytes released");
    Memory m{allocations,total_bytes,peak_bytes,retained};
    std::printf("memory %s calls=%zu total=%zu peak=%zu retained=%zu\n",name,m.calls,m.total,m.peak,m.retained);return m;
}
void memory_gates() {
    Fixture f(4096),big(65536),prefix(4097);
    for(size_t i=0;i<4096;++i)prefix.nodes[i]->type=AETHER_NODE_TEXT;
    auto nth=measure("nth4096",[&](auto& c){for(size_t i=0;i<f.nodes.size();++i)check(c.position(f.nodes[i].get())==i+1,"nth memory result");check(parents(c).empty(),"nth-only leaves adjacent absent");});
    auto plus=measure("adjacent4096",[&](auto& c){for(size_t i=0;i<f.nodes.size();++i)check(c.previous_sibling(f.nodes[i].get())==(i?f.nodes[i-1].get():nullptr),"adjacent memory result");check(selector_match_context_test_access::nth_empty(c),"adjacent-only leaves nth absent");});
    auto mixed=measure("mixed4096",[&](auto& c){for(size_t i=0;i<f.nodes.size();++i){check(c.position(f.nodes[i].get())==i+1,"mixed nth");check(c.previous_sibling(f.nodes[i].get())==(i?f.nodes[i-1].get():nullptr),"mixed adjacent");}});
    check(nth.calls==plus.calls && nth.total==plus.total && nth.peak==plus.peak,"dedicated predecessor storage equals unchanged nth heap budget");
    check(mixed.calls==2*nth.calls && mixed.total==2*nth.total && mixed.retained==2*nth.retained,"mixed two independent indexes explicit cost");
    check(mixed.peak<=2*nth.peak && mixed.peak>nth.peak,"mixed peak includes overlapping full and temporary maps");
    // Fixed requested-byte baselines are for this 64-bit libstdc++ ABI only.
    // Other standard libraries still run portable equality/reclamation gates.
#if defined(__GLIBCXX__) && INTPTR_MAX == INT64_MAX
    check(nth.calls==4368 && nth.total==185160 && nth.peak==147400,"unchanged libstdc++ nth baseline");
    check(mixed.calls==8736 && mixed.peak==286600,"mixed peak 1.944x is not hidden");
    // The ancestor-ID experiment adds bounded private scratch to the 128-byte baseline.
    check(sizeof(selector_match_context_s)>=128 && sizeof(selector_match_context_s)<=256,
          "context growth stays within the reviewed additional 128-byte budget");
#endif
    std::printf("context sizeof=%zu mixed/nth heap peak=%.6f\n",sizeof(selector_match_context_s),double(mixed.peak)/double(nth.peak));
    auto cheap=measure("cheap33x8/65536",[&](auto& c){for(int i=0;i<8;++i)(void)c.previous_sibling(big.nodes[32].get());});
    check(!cheap.calls && !cheap.peak && !cheap.total,"cheap allocation budget zero");
    auto tail=measure("tailx1/65536",[&](auto& c){(void)c.previous_sibling(big.nodes.back().get());});
    auto repeated=measure("tailx8/65536",[&](auto& c){for(int i=0;i<8;++i)(void)c.previous_sibling(big.nodes.back().get());});
    auto text=measure("4096text-null-hitx8",[&](auto& c){for(int i=0;i<8;++i)check(c.previous_sibling(prefix.nodes.back().get())==nullptr,"null memo memory");});
    check(tail.calls==repeated.calls && tail.total==repeated.total && tail.peak==repeated.peak,"memo hits no heap growth");
    check(tail.calls==text.calls && tail.total==text.total && tail.peak==text.peak,"dense text null memo has one-record budget");
}
void oom(bool mixed) {
    Fixture f(256);auto original=f.parent.children;
    auto selector=parse(mixed?"div:nth-child(n) + div:nth-child(n)":"div + div");
    auto run=[&](auto& c){for(size_t i=1;i<f.nodes.size();++i)check(selector_match(selector,f.nodes[i].get(),c),"OOM matcher never silently false");};
    size_t total=0;
    {selector_match_context_s c;allocations=0;track=true;run(c);track=false;total=allocations;check(parents(c).at(&f.parent).complete,"successful OOM reference completes");}
    check(live_bytes==0,"reference allocation reclaimed");check(total>256,"OOM covers metadata memo rehash and full-map allocations");
    size_t partial=0;
    for(size_t failure=0;failure<total;++failure) {
        {
            selector_match_context_s c;bool threw=false;fail_at=failure;allocations=0;track=true;
            try {run(c);} catch(const std::bad_alloc&) {threw=true;}
            track=false;fail_at=std::numeric_limits<size_t>::max();
            check(threw,"each allocation ordinal propagates bad_alloc");
            // Mixed nth may fail after adjacent already published successfully;
            // audit completeness rather than incorrectly requiring all incomplete.
            for(const auto& [p,e]:parents(c)) {
                if(!e.complete && !e.previous.empty())++partial;
                if(!mixed)check(!e.complete,"failed adjacent build never publishes incomplete full map");
            }
            audit(c);check(f.parent.children==original,"OOM leaves topology unchanged");
            for(auto& n:f.nodes)check(n->parent==&f.parent,"OOM preserves parent identity");
        }
        check(live_bytes==0,"failed scope frees every tracked allocation");
        selector_match_context_s retry;run(retry);check(parents(retry).at(&f.parent).complete,"fresh retry succeeds after each OOM");
    }
    check(partial>256,"OOM includes temporary complete-map phase");
    std::printf("OOM %s allocation points=%zu\n",mixed?"mixed":"adjacent",total);
}
}
int main() {
    activation();differential();membership_and_lifetime();
    trace(512,0,0,16983);trace(4096,0,0,78016);trace(8192,0,0,147848);trace(8192,0,1,7873024);
    trace(65536,0,3,65536);trace(65536,0,4,66);trace(65536,0,5,2096656);
    for(int shape=0;shape<3;++shape)for(int order=0;order<3;++order)trace(1024,shape,order);
    memory_gates();oom(false);oom(true);
    std::printf("adjacent cache: %zu checks, %zu failures\n",checks,failures);return failures?1:0;
}
