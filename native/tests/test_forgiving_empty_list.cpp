#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <vector>
#include <stdexcept>
#include <string>

namespace {
int assertions = 0, fixtures = 0, resolutions = 0, failures = 0;
void check(bool ok, const char* message) {
    ++assertions;
    if (!ok) throw std::runtime_error(message);
}
struct Values { float opacity, width, height; uint32_t impact; };
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    std::map<aether_node_t, Values> styles;
    std::map<aether_node_t, Values> changed;
    std::set<aether_node_t> authored;
    std::map<aether_node_t, std::vector<std::pair<std::string, float>>> generated;
    Fixture(const char* markup, const char* css, int expected_rules) {
        ++fixtures;
        try {
            check(aether_doc_create(&doc) == AETHER_OK, "create document");
            check(aether_doc_parse_html(doc, markup) == AETHER_OK, "parse HTML");
            collect(reinterpret_cast<aether_node_t>(doc));
            aether_style_runtime_options_t options{};
            const aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
            check(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK, "create runtime");
            int64_t sheet = 0; int32_t rules = -1;
            check(aether_style_runtime_add_stylesheet(runtime,
                ".card{opacity:.2;width:10px;height:11px}", 2, "base", &sheet, &rules) == AETHER_OK, "base sheet");
            check(aether_style_runtime_add_stylesheet(runtime, css, 2, "guard", &sheet, &rules) == AETHER_OK, "test sheet");
            check(sheet != 0, "test sheet has a handle even when all selectors reject");
            check(rules == expected_rules, "test sheet rule count (base excluded)");
        } catch (...) { cleanup(); throw; }
    }
    void collect(aether_node_t n) {
        authored.insert(n);
        for (int i=0; i<aether_node_get_child_count(n); ++i) collect(aether_node_get_child(n,i));
    }
    void cleanup() {
        if (runtime) aether_style_runtime_destroy(runtime);
        if (doc) aether_doc_destroy(doc);
        runtime = nullptr; doc = nullptr;
    }
    ~Fixture() { cleanup(); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    aether_node_t find(const char* name) {
        return aether_doc_get_element_by_id(doc, name);
    }
    int64_t add(const char* css, int expected) {
        int64_t handle = 0; int32_t rules = -1;
        check(aether_style_runtime_add_stylesheet(runtime, css, 2, "extra", &handle, &rules) == AETHER_OK, "add sheet");
        check(handle != 0, "nonzero sheet handle, including zero-rule sheets");
        check(rules == expected, "extra sheet rule count");
        return handle;
    }
    void dimensions(const char* target, float width, float height) {
        const auto& value = styles.at(find(target));
        check(std::fabs(value.width - width) < .0001f, "computed width declaration");
        check(std::fabs(value.height - height) < .0001f, "computed height declaration");
    }
    int resolve(aether_node_t leaf = nullptr) {
        ++resolutions;
        changed.clear(); generated.clear();
        aether_style_update_t* updates = nullptr;
        auto root = reinterpret_cast<aether_node_t>(doc);
        // The return value is the output update count; the input count is one.
        int count = leaf ? aether_style_runtime_resolve_state_changes(runtime, root, &leaf, 1, &updates)
                         : aether_style_runtime_resolve_subtree(runtime, root, &updates);
        try {
            check(count >= 0, "resolve status");
            check((count == 0) == (updates == nullptr), "output pointer matches update count");
            for (int i = 0; i < count; ++i) {
                aether_node_t node = nullptr;
                aether_computed_style_handle_t handle = nullptr;
                uint32_t impact = 0;
                const auto status = aether_style_runtime_get_update(updates[i], &node, &handle, &impact);
                if (status != AETHER_OK || !handle) {
                    if (handle) aether_computed_style_handle_destroy(handle);
                    check(false, "get current update");
                }
                aether_computed_style_t style{};
                const auto copied = aether_computed_style_copy(handle, &style);
                aether_computed_style_handle_destroy(handle);
                check(copied == AETHER_OK, "copy current update");
                Values value{style.opacity, style.width.value, style.height.value, impact};
                aether_computed_style_free(&style);
                if (authored.count(node)) { styles[node] = value; changed[node] = value; }
                else {
                    auto parent = aether_node_get_parent(node);
                    if (authored.count(parent)) {
                        char* text = aether_node_get_text_content(aether_node_get_child(node,0));
                        const std::string content = text ? text : "";
                        aether_string_free(text);
                        generated[parent].emplace_back(content,value.opacity);
                    }
                }
            }
        } catch (...) { aether_style_updates_free(updates, count); throw; }
        aether_style_updates_free(updates, count);
        return count;
    }
    void expect(const char* target, float opacity) {
        auto node = find(target); // Query after Resolve; cached values own only scalar snapshots.
        check(node && styles.count(node) == 1, "fresh target has computed style");
        const auto& s = styles.at(node);
        check(std::fabs(s.opacity - opacity) < .0001f, "computed opacity");
    }
};
// Public ABI integration: acceptance alone is insufficient; observe styles and updates.
const char* markup = "<body><section id=ancestor class=ancestor><div id=card class='card safe'>"
    "<span id=leaf class=ready data-live=yes></span></div><div id=empty class=card></div>"
    "<div id=text class=card>text</div></section></body>";
template<class Action> void run(const std::string& name, Action action) {
    try { action(); std::printf("FORGIVING PASS %s\n", name.c_str()); }
    catch (const std::exception& e) { ++failures; std::fprintf(stderr,"FORGIVING FAIL %s: %s\n",name.c_str(),e.what()); }
}
void one(const std::string& selector, int rules, float card, float empty) {
    Fixture f(markup, (selector+"{opacity:.8}").c_str(),rules);
    f.resolve(); f.expect("card",card); f.expect("empty",empty); f.expect("text",empty);
    check(f.resolve() == 0,"repeated resolution no-op");
    f.expect("card",card); f.expect("empty",empty);
}
void matrices() {
    for (const std::string fn : {"is","where"}) {
        for (const std::string body : {"", " ", "\t\r\n\f", "/**/", ",", ",,", ":unknown",
                ":unknown,:nth-child(bogus)", "#ghost:hover:unknown", "[x=]", ".bad >", "> .ready",
                ":hover()", ":not()", ":has()", ":has(.ready,:unknown)", ":not(.safe,:unknown)"}) {
            const auto inner = ":"+fn+"("+body+")";
            run("empty/"+inner,[&]{one(".card"+inner,1,.2f,.2f);});
            run("negated/"+inner,[&]{one(".card:not("+inner+")",1,.8f,.8f);});
        }
    }
    struct Case { const char* selector; int rules; float card,empty; };
    for (auto c : std::vector<Case>{
            {".card:not(:not(:is()))",1,.2f,.2f}, {".card:is(:where())",1,.2f,.2f},
            {".card:has(:is())",1,.2f,.2f}, {".card:has(:not(:where()))",1,.8f,.2f},
            {".card:not(:has(:is()))",1,.8f,.8f}, {".card:not(:is(),.skip)",1,.8f,.8f},
            {".card:has(:where(),.ready)",1,.8f,.2f}, {".card:is(.safe,#ghost:is())",1,.8f,.2f},
            {".card:is()",1,.2f,.2f}, {".card:not()",0,.2f,.2f}, {".card:has()",0,.2f,.2f},
            {".card:not(:is(),)",0,.2f,.2f}, {".card:has(:where(),:unknown)",0,.2f,.2f},
            {".card:not(.safe,:unknown)",0,.2f,.2f}, {".card:has(.ready,:unknown)",0,.2f,.2f},
            {".card:IS()",1,.2f,.2f}, {R"(.card:not(:\77 here()))",1,.8f,.8f},
            {".card:is(),.card",2,.8f,.8f}, {".card:where(),.card",2,.8f,.8f}})
        run(c.selector,[&]{one(c.selector,c.rules,c.card,c.empty);});
}
void cascade_and_media() {
    for (const std::string tie : {".card:not(:is(#ghost:hover:unknown))", ".card:not(:where())"}) {
        for (int reverse : {0,1}) {
            const std::string a=tie+"{opacity:.8}", b=".card{opacity:.4}";
            Fixture f(markup,(reverse ? b+a : a+b).c_str(),2);
            f.resolve(); f.expect("card",reverse ? .8f : .4f);
        }
    }
    for (int reverse : {0,1}) {
        const std::string specific=".card:is(.safe,#ghost:is()){opacity:.8}", plain=".card.safe{opacity:.4}";
        Fixture f(markup,(reverse ? plain+specific : specific+plain).c_str(),2);
        f.resolve(); f.expect("card",.8f); f.expect("empty",.2f);
        const std::string zero=".card:where(.safe,#ghost:is()){opacity:.8}";
        Fixture w(markup,(reverse ? plain+zero : zero+plain).c_str(),2);
        w.resolve(); w.expect("card",.4f); w.expect("empty",.2f);
    }
    Fixture f(markup,":is(),.card{opacity:.3;width:30px}"
        "@media(min-width:700px){:where(),.card:not(:is()){opacity:.6;width:60px}}"
        "@media(max-width:600px){:is(),.card:not(:where()){opacity:.9;width:90px}}",6);
    f.resolve(); f.expect("card",.6f); f.dimensions("card",60,11);
    check(f.resolve()==0,"media stable");
}
void sheets() {
    Fixture f(markup,".card{opacity:.4}",1); f.resolve(); f.expect("card",.4f);
    for (int round=0; round<4; ++round) {
        auto never=f.add(".card:is(){opacity:.99;width:999px}",1);
        auto invalid=f.add(".card:has(.ready,:unknown){opacity:.99}",0);
        auto valid=f.add(".card:not(:where()){opacity:.8}",1);
        check(never!=invalid && never!=valid && invalid!=valid,"distinct sheet handles");
        f.resolve(); f.expect("card",.8f); f.dimensions("card",10,11);
        check(f.resolve()==0,"added sheet repeat no-op");
        check(aether_style_runtime_remove_stylesheet(f.runtime,never),"remove never-matching sheet");
        check(!aether_style_runtime_remove_stylesheet(f.runtime,never),"removed never-matching handle dead");
        f.resolve(); f.expect("card",.8f);
        check(aether_style_runtime_remove_stylesheet(f.runtime,valid),"remove matching sheet");
        f.resolve(); f.expect("card",.4f);
        check(aether_style_runtime_remove_stylesheet(f.runtime,invalid),"remove zero-rule strict sheet");
        f.resolve(); f.expect("card",.4f); check(f.resolve()==0,"removed sheets repeat no-op");
    }
}
void state_changes() {
    struct State { const char* pseudo; int bit; };
    for (auto state : std::vector<State>{{"hover",AETHER_STATE_HOVER},{"focus",AETHER_STATE_FOCUS},
                                      {"active",AETHER_STATE_ACTIVE},{"invalid",AETHER_STATE_INVALID}}) {
        const std::string not_state=":not(:is(:unknown,:"+std::string(state.pseudo)+"))";
        for (int context=0; context<3; ++context) {
            const auto selector=context==0 ? ".card"+not_state :
                context==1 ? ".ancestor"+not_state+" .card" : ".card:has(.ready"+not_state+")";
            Fixture f(markup,(selector+"{opacity:.8}").c_str(),1);
            auto changed=f.find(context==0 ? "card" : context==1 ? "ancestor" : "leaf");
            f.resolve(); f.expect("card",.8f);
            for (int on : {1,0,1,0}) {
                aether_element_set_state(changed,on ? state.bit : AETHER_STATE_NONE);
                check(f.resolve(changed)>0,"state change publishes affected style");
                f.expect("card",on ? .2f : .8f);
                check(f.changed.count(f.find("card"))==1,"nonlocal state discovers target");
                check(f.changed.at(f.find("card")).impact==0x0C,"opacity impact stays composite/hittest");
                check(f.resolve(changed)==0,"unchanged state repeat no-op");
            }
        }
        const auto discarded=".card:not(:is(#ghost:"+std::string(state.pseudo)+":unknown)){opacity:.8}";
        Fixture d(markup,discarded.c_str(),1); d.resolve(); d.expect("card",.8f);
        for (int on : {1,0}) {
            auto card=d.find("card"); aether_element_set_state(card,on ? state.bit : AETHER_STATE_NONE);
            check(d.resolve(card)==0,"rejected nested state does not change style"); d.expect("card",.8f);
        }
    }

}
void mutations() {
    Fixture f(markup,".card:has(#leaf.ready[data-live=yes]:not(:is())){opacity:.8}",1);
    auto card=f.find("card"), other=f.find("empty"), leaf=f.find("leaf");
    f.resolve(); f.expect("card",.8f); f.expect("empty",.2f);
    for (int round=0; round<3; ++round) {
        check(aether_element_set_attribute(leaf,"class","absent")==AETHER_OK,"remove matching class");
        f.resolve(); f.expect("card",.2f);
        check(aether_element_set_attribute(leaf,"class","ready")==AETHER_OK,"restore matching class");
        f.resolve(); f.expect("card",.8f);
        check(aether_element_set_attribute(leaf,"id","other-id")==AETHER_OK,"remove matching ID");
        f.resolve(); f.expect("card",.2f);
        check(aether_element_set_attribute(leaf,"id","leaf")==AETHER_OK,"restore matching ID");
        f.resolve(); f.expect("card",.8f);
        check(aether_element_set_attribute(leaf,"data-live","no")==AETHER_OK,"remove matching attribute");
        f.resolve(); f.expect("card",.2f);
        check(aether_element_set_attribute(leaf,"data-live","yes")==AETHER_OK,"restore matching attribute");
        f.resolve(); f.expect("card",.8f);
        check(aether_node_append_child(other,leaf)==AETHER_OK,"move real descendant");
        f.resolve(); f.expect("card",.2f); f.expect("empty",.8f);
        check(aether_node_append_child(card,leaf)==AETHER_OK,"restore real descendant");
        f.resolve(); f.expect("card",.8f); f.expect("empty",.2f);
    }
    Fixture host(markup,".card:not(:is()){opacity:.8}",1); host.resolve(); host.expect("card",.8f);
    check(aether_element_set_attribute(host.find("card"),"class","absent")==AETHER_OK,"remove outer subject class");
    host.resolve(); host.expect("card",1.f);
    check(aether_element_set_attribute(host.find("card"),"class","card")==AETHER_OK,"restore outer subject class");
    host.resolve(); host.expect("card",.8f);
}
void generated_content() {
    Fixture f(markup,".card:has(:not(:is())){opacity:.8}",1);
    for (const char* pseudo : {"before","after"}) {
        const std::string css=".card:not(:is())::"+std::string(pseudo)+"{content:'X';opacity:.7}";
        auto handle=f.add(css.c_str(),1);
        f.resolve(); f.expect("card",.8f); f.expect("empty",.2f); f.expect("text",.2f);
        for (const char* name : {"card","empty","text"}) {
            const auto& values=f.generated[f.find(name)];
            check(values.size()==1 && values[0].first=="X" && std::fabs(values[0].second-.7f)<.0001f,
                  "generated selector matches host NOT-empty and retains content/style");
        }
        check(aether_node_get_child_count(f.find("empty"))==0,"generated excluded from public DOM children");
        check(aether_node_get_child_count(f.find("text"))==1,"text-only DOM children stable");
        // Generated nodes may be recreated on resolve; compare copied values,
        // not update count or handles that belong to the previous resolution.
        f.resolve(); f.expect("card",.8f); f.expect("empty",.2f); f.expect("text",.2f);
        for (const char* name : {"card","empty","text"}) {
            const auto& values=f.generated[f.find(name)];
            check(values.size()==1 && values[0].first=="X" && std::fabs(values[0].second-.7f)<.0001f,
                  "generated repeated resolution preserves content/style");
        }
        check(aether_style_runtime_remove_stylesheet(f.runtime,handle),"remove generated sheet");
        f.resolve(); f.expect("card",.8f); f.expect("empty",.2f); f.expect("text",.2f);
    }
}
} // namespace
int main() {
    matrices();
    run("cascade-and-media",cascade_and_media); run("sheet-lifecycle",sheets);
    run("subject-ancestor-descendant-state-delta",state_changes);
    run("class-id-attribute-structure",mutations); run("generated-content",generated_content);
    std::printf("FORGIVING_EMPTY_LIST FIXTURES=%d RESOLUTIONS=%d ASSERTIONS=%d FAILURES=%d\n",
                fixtures,resolutions,assertions,failures);
    return failures ? 1 : 0;
}
