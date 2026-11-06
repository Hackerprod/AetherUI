#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

// Public ABI only; no assert(), so every oracle remains active under NDEBUG.
// Reentrancy is tested only during add_stylesheet diagnostics. No callback
// destroys its runtime/user data or throws across the C ABI.
namespace {
int checks = 0, failures = 0;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL %s\n", label); }
}
void near(float actual, float expected, const char* label) {
    ++checks;
    if (!(std::fabs(actual - expected) < .0001f)) {
        ++failures; std::fprintf(stderr, "FAIL %s: %g != %g\n", label, actual, expected);
    }
}
void setup(bool ok) { if (!ok) { std::fprintf(stderr, "HARNESS setup failed\n"); std::exit(2); } }
struct Event { std::string code, property, value, message; };
const char* const calc_message = "The calc() expression is syntactically or dimensionally invalid.";
void payload(const Event& e, const char* property, const char* value) {
    check(e.code == "AETHER-CSS-CALC", "exact diagnostic code");
    check(e.property == property, "exact diagnostic property");
    check(e.value == value, "exact diagnostic value");
    check(e.message == calc_message, "exact diagnostic message");
}
struct Callback {
    std::vector<Event> events;
    std::function<void(size_t)> action;
    static void receive(const char* code, const char* property, const char* value, const char* message, void* data) {
        auto& self = *static_cast<Callback*>(data);
        check(code && property && value && message, "calc payload pointers valid");
        // Copy during callback; never keep the borrowed pointers.
        self.events.push_back({code ? code : "", property ? property : "", value ? value : "", message ? message : ""});
        const size_t index = self.events.size() - 1;
        if (self.action) self.action(index);
    }
};
struct Values { float opacity = -1, width = -1, top = -1, right = -1, bottom = -1, left = -1; };
Values copy(aether_computed_style_handle_t handle) {
    check(handle != nullptr, "owned style snapshot exists");
    if (!handle) return {};
    aether_computed_style_t style{};
    setup(aether_computed_style_copy(handle, &style) == AETHER_OK);
    Values v{style.opacity, style.width.value, style.margin.top.value, style.margin.right.value,
             style.margin.bottom.value, style.margin.left.value};
    aether_computed_style_free(&style); return v;
}
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t rt = nullptr;
    aether_node_t target = nullptr;
    aether_computed_style_handle_t latest = nullptr;
    std::string generated;
    float pseudo_opacity = -1;
    Fixture() {
        setup(aether_doc_create(&doc) == AETHER_OK);
        setup(aether_doc_parse_html(doc, "<body><div id='target' class='a b'></div></body>") == AETHER_OK);
        target = aether_doc_get_element_by_id(doc, "target"); setup(target != nullptr);
        aether_style_environment_t env{800,600,1,16,0,0,0};
        setup(aether_style_runtime_create(nullptr, &env, nullptr, nullptr, &rt) == AETHER_OK);
    }
    ~Fixture() { aether_computed_style_handle_destroy(latest); aether_style_runtime_destroy(rt); aether_doc_destroy(doc); }
    Fixture(const Fixture&) = delete;
    void callback(Callback* c) { aether_style_runtime_set_diagnostics_callback(rt, c ? Callback::receive : nullptr, c); }
    aether_status_t add(const char* css, int64_t& handle, int32_t& count) {
        return aether_style_runtime_add_stylesheet(rt, css, 2, "reentrant.css", &handle, &count);
    }
    int64_t add(const char* css) { int64_t h=-1; int32_t n=-1; check(add(css,h,n)==AETHER_OK,"add status OK"); return h; }
    void remove(int64_t handle) { check(aether_style_runtime_remove_stylesheet(rt,handle)==AETHER_TRUE,"remove existing handle"); }
    Values read(bool clear = true, aether_computed_style_handle_t* snapshot = nullptr) {
        if (clear) aether_style_runtime_clear_cache(rt);
        aether_style_update_t* updates = nullptr;
        int n = aether_style_runtime_resolve_subtree(rt,aether_doc_get_body(doc),&updates);
        check(n >= 0,"resolve succeeds"); generated.clear(); pseudo_opacity=-1;
        for (int i=0;i<n;++i) {
            aether_node_t node = nullptr; aether_computed_style_handle_t h = nullptr;
            setup(aether_style_runtime_get_update(updates[i],&node,&h,nullptr)==AETHER_OK);
            if (node == target) {
                aether_computed_style_handle_destroy(latest); latest=h;
                if (snapshot) setup(aether_style_runtime_get_update(updates[i],nullptr,snapshot,nullptr)==AETHER_OK);
            } else {
                if (aether_node_get_parent(node)==target) {
                    char* text=aether_node_get_text_content(aether_node_get_child(node,0));
                    generated=text?text:""; aether_string_free(text); pseudo_opacity=copy(h).opacity;
                }
                aether_computed_style_handle_destroy(h);
            }
        }
        aether_style_updates_free(updates,n); return copy(latest);
    }
};
void original_and_committed_outputs() {
    Fixture f; Callback c; f.callback(&c);
    int64_t outer=-11,nested=-12; int32_t count=-13,nested_count=-14;
    aether_computed_style_handle_t before=nullptr,after=nullptr;
    c.action=[&](size_t index) {
        if(index!=0) return; // Explicit guard even on a broken implementation.
        check(outer>0,"R3 outer output published before callback"); check(count==2,"R3 complete outer rule count published");
        auto v=f.read(false,&before); near(v.opacity,.4f,"R3 callback sees complete outer"); near(v.width,14,"R3 rule after diagnostic visible");
        check(f.add(".a{opacity:.8}",nested,nested_count)==AETHER_OK,"R1 nested status");
        near(f.read(false,&after).opacity,.8f,"R3 callback nested winner");
    };
    check(f.add(".a{width:calc(1px + 1deg);opacity:.4}.a{width:14px}",outer,count)==AETHER_OK,"R1 outer status");
    check(c.events.size()==1,"R1 callback synchronous exactly once");
    if(!c.events.empty()) payload(c.events[0],"width","calc(1px + 1deg)");
    check(nested>outer && outer>0,"R1 distinct ordered handles"); check(nested_count==1,"R1 nested rule count");
    near(f.read(false).opacity,.8f,"R1 final nested winner");
    near(copy(before).opacity,.4f,"R3 outer snapshot survives return"); near(copy(after).opacity,.8f,"R3 nested snapshot survives return");
    f.remove(outer); near(f.read().opacity,.8f,"R1 nested survives outer removal");
    f.remove(nested); near(f.read().opacity,1,"R1 nested independent removal");
    near(copy(before).width,14,"R3 retained snapshot survives all removals");
    aether_computed_style_handle_destroy(before); aether_computed_style_handle_destroy(after);
}
void fifo_and_depth() {
    {
        Fixture f; Callback c; f.callback(&c); std::vector<int64_t> nested;
        c.action=[&](size_t i) { if(i<2) nested.push_back(f.add(i==0?".a{opacity:.8}":".a{opacity:.9}")); };
        auto outer=f.add(".a{width:calc(1px + 1deg);height:calc(2px + 2deg);opacity:.4}");
        check(c.events.size()==2,"R4 two synchronous payloads");
        if(c.events.size()==2) { payload(c.events[0],"width","calc(1px + 1deg)"); payload(c.events[1],"height","calc(2px + 2deg)"); }
        check(nested.size()==2,"R4 both nested adds performed");
        if(nested.size()==2) {
            check(outer<nested[0] && nested[0]<nested[1],"R4 unique ordered handles");
            near(f.read().opacity,.9f,"R4 last nested wins"); f.remove(outer); near(f.read().opacity,.9f,"R4 both nested survive outer");
            f.remove(nested[1]); near(f.read().opacity,.8f,"R4 first nested survives second removal");
            f.remove(nested[0]); near(f.read().opacity,1,"R4 independent final removal");
        }
    }
    {
        Fixture f; Callback c; f.callback(&c);
        int depth=0; int64_t handles[3]={-1,-1,-1}; int32_t counts[3]={-1,-1,-1};
        std::vector<int> trace;
        c.action=[&](size_t) {
            int level=depth; trace.push_back(level);
            check(level<3,"R5 bounded recursion");
            if(level>=3) return;
            check(handles[level]>0 && counts[level]==1,"R5 own outputs committed at each level");
            if(level<2) {
                ++depth;
                check(f.add(level==0?".a{height:calc(2px + 2deg);opacity:.6}":".a{width:calc(3px + 3deg);opacity:.8}",handles[level+1],counts[level+1])==AETHER_OK,"R5 nested add succeeds");
                --depth;
            }
            trace.push_back(level+10);
        };
        check(f.add(".a{width:calc(1px + 1deg);opacity:.4}",handles[0],counts[0])==AETHER_OK,"R5 outer success");
        check(trace==std::vector<int>({0,1,2,12,11,10}),"R5 synchronous depth-first replay");
        check(c.events.size()==3,"R5 exactly three diagnostics");
        check(handles[0]<handles[1] && handles[1]<handles[2],"R5 ordered disjoint handles");
        near(f.read().opacity,.8f,"R5 deepest winner"); f.remove(handles[2]); near(f.read().opacity,.6f,"R5 middle survives");
        f.remove(handles[1]); near(f.read().opacity,.4f,"R5 outer survives"); f.remove(handles[0]); near(f.read().opacity,1,"R5 all removed");
    }
}
void callback_mutations() {
    {
        Fixture f; Callback c; f.callback(&c); int64_t outer=-1,replacement=-1; int32_t count=-1;
        c.action=[&](size_t i) { if(i==0) { f.remove(outer); replacement=f.add(".a{opacity:.9}"); near(f.read(false).opacity,.9f,"R6 replacement visible in callback"); } };
        check(f.add(".a{width:calc(1px + 1deg);opacity:.4}",outer,count)==AETHER_OK,"R6 removed outer still returns OK");
        check(replacement>outer,"R6 replacement later handle");
        check(aether_style_runtime_remove_stylesheet(f.rt,outer)==AETHER_FALSE,"R6 outer not reinserted");
        near(f.read().opacity,.9f,"R6 replacement survives return"); f.remove(replacement); near(f.read().opacity,1,"R6 replacement independently removable");
    }
    {
        Fixture f; auto prior=f.add(".a{opacity:.2}"); Callback c; f.callback(&c);
        c.action=[&](size_t i){if(i==0)f.remove(prior);};
        auto outer=f.add(".a{width:calc(1px + 1deg);opacity:.4}");
        near(f.read().opacity,.4f,"R6 remove prior preserves outer"); f.remove(outer); near(f.read().opacity,1,"R6 prior remains removed");
    }
    {
        Fixture f; Callback c; f.callback(&c); aether_computed_style_handle_t snapshot=nullptr;
        c.action=[&](size_t i){if(i==0)near(f.read(true,&snapshot).opacity,.4f,"R6 clear and resolve callback sees committed outer");};
        f.add(".a{width:calc(1px + 1deg);opacity:.4}");
        near(copy(snapshot).opacity,.4f,"R6 cache-only callback snapshot survives"); near(f.read(false).opacity,.4f,"R6 cache-only final value");
        aether_computed_style_handle_destroy(snapshot);
    }
}
void callback_capture_and_other_runtime() {
    {
        Fixture f; Callback c1,c2; std::vector<int> recipients;
        c2.action=[&](size_t){recipients.push_back(2);};
        c1.action=[&](size_t i){recipients.push_back(1); if(i==0){f.callback(&c2); f.add(".a{width:calc(3px + 3deg);opacity:.8}");}};
        f.callback(&c1); f.add(".a{width:calc(1px + 1deg);height:calc(2px + 2deg);opacity:.4}");
        check(recipients==std::vector<int>({1,2,1}),"R11 current batch retains original callback and user data");
        check(c1.events.size()==2 && c2.events.size()==1,"R11 callback batches remain separate");
        if(c1.events.size()==2){payload(c1.events[0],"width","calc(1px + 1deg)");payload(c1.events[1],"height","calc(2px + 2deg)");}
        if(!c2.events.empty())payload(c2.events[0],"width","calc(3px + 3deg)");
        near(f.read().opacity,.8f,"R11 nested override preserved");
        f.add(".a{height:calc(4px + 4deg)}"); check(c2.events.size()==2,"R11 future add uses replacement");
    }
    {
        Fixture f; Callback c; f.callback(&c);
        c.action=[&](size_t i){if(i==0){f.callback(nullptr); f.add(".a{width:calc(3px + 3deg);opacity:.8}");}};
        f.add(".a{width:calc(1px + 1deg);height:calc(2px + 2deg);opacity:.4}");
        check(c.events.size()==2,"R11 null replacement does not alter existing batch");
        f.add(".a{width:calc(4px + 4deg)}"); check(c.events.size()==2,"R11 new calls honor null callback");
        near(f.read().opacity,.8f,"R11 null-callback nested survives");
    }
    {
        Fixture a,b; Callback ca,cb; std::vector<int> order; b.callback(&cb);
        cb.action=[&](size_t){order.push_back(2);};
        ca.action=[&](size_t i){order.push_back(1);if(i==0)b.add(".a{width:calc(3px + 3deg);opacity:.8}");};
        a.callback(&ca); a.add(".a{width:calc(1px + 1deg);height:calc(2px + 2deg);opacity:.4}");
        check(order==std::vector<int>({1,2,1}),"R12 separate runtimes replay synchronously without mixing");
        check(ca.events.size()==2 && cb.events.size()==1,"R12 separate payload storage");
        if(ca.events.size()==2)payload(ca.events[1],"height","calc(2px + 2deg)");
        if(!cb.events.empty())payload(cb.events[0],"width","calc(3px + 3deg)");
        near(a.read().opacity,.4f,"R12 first runtime unchanged"); near(b.read().opacity,.8f,"R12 second runtime updated");
    }
}
void all_consumers_and_passive() {
    const char* outer=".a{width:calc(1px + 1deg);opacity:.4;--w:4px;--edges:4px;width:var(--w);margin:var(--edges)}.a::before{content:'old';opacity:.4}";
    const char* nested=".a{opacity:.8;--w:8px;--edges:1px 2px 3px 8px;margin:var(--edges)}.a::before{--word:'new';content:var(--word);opacity:.8}";
    for(int mode=0;mode<3;++mode){
        Fixture f; Callback c; int64_t nested_handle=-1;
        // Null and passive paths perform the same adds without recursion;
        // nested mode performs the second add inside the diagnostic callback.
        if(mode!=0)f.callback(&c);
        if(mode==2)c.action=[&](size_t i){if(i==0)nested_handle=f.add(nested);};
        int64_t outer_handle=f.add(outer); if(mode!=2)nested_handle=f.add(nested);
        check(outer_handle==1 && nested_handle==2,"R13 null/passive/reentrant handle parity");
        check(c.events.size()==(mode==0?0u:1u),"R13 callback count");
        auto v=f.read(); near(v.opacity,.8f,"R13 ordinary override"); near(v.width,8,"R13 custom override");
        near(v.top,1,"R13 pending top");near(v.right,2,"R13 pending right");near(v.bottom,3,"R13 pending bottom");near(v.left,8,"R13 pending left");
        check(f.generated=="new","R13 content override");near(f.pseudo_opacity,.8f,"R13 pseudo ordinary override");
        check(aether_node_get_child_count(f.target)==0,"R13 generated nodes not authored children");
        f.remove(outer_handle); near(f.read().opacity,.8f,"R13 nested survives outer removal");
        f.remove(nested_handle); near(f.read().opacity,1,"R13 all consumers removable");
    }
}
}
int main() {
    original_and_committed_outputs(); fifo_and_depth(); callback_mutations();
    callback_capture_and_other_runtime(); all_consumers_and_passive();
    std::printf("stylesheet diagnostic reentrancy: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
