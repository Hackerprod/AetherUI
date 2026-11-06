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
struct Generated { std::string text; float opacity, font_size; };
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    std::map<aether_node_t, Values> styles;
    std::map<aether_node_t, Values> changed;
    std::set<aether_node_t> authored;
    std::map<aether_node_t, std::vector<Generated>> generated;
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
                ".card{opacity:.2;width:10px;height:11px;font-size:23px}", 2, "base", &sheet, &rules) == AETHER_OK, "base sheet");
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
                if (status != AETHER_OK || !node || !handle) {
                    if (handle) aether_computed_style_handle_destroy(handle);
                    check(false, "get current update");
                }
                aether_computed_style_t style{};
                const auto copied = aether_computed_style_copy(handle, &style);
                aether_computed_style_handle_destroy(handle);
                check(copied == AETHER_OK, "copy current update");
                Values value{style.opacity, style.width.value, style.height.value, impact};
                const float font_size = style.font_size;
                aether_computed_style_free(&style);
                if (authored.count(node)) { styles[node] = value; changed[node] = value; }
                else {
                    auto parent = aether_node_get_parent(node);
                    if (authored.count(parent)) {
                        const auto children = aether_node_get_child_count(node);
                        check(children == 0 || children == 1, "generated pseudo owns at most one text child");
                        std::string content;
                        if (children == 1) {
                            auto child = aether_node_get_child(node,0);
                            check(child && aether_node_get_parent(child) == node, "generated text parent");
                            check(aether_node_get_type(child) == AETHER_NODE_TEXT, "generated child is text");
                            char* text = aether_node_get_text_content(child);
                            check(text != nullptr, "generated text can be read");
                            content = text;
                            aether_string_free(text);
                        }
                        // Empty content still creates a pseudo, but has no text child.
                        generated[parent].push_back({content,value.opacity,font_size});
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
// Only public ABI. Checks and all side effects remain active under NDEBUG.
const char* markup = "<body><section id=ancestor><div id=card class='card safe'>"
    "<span id=leaf class=ready data-live=yes></span></div><div id=empty class=card></div>"
    "<div id=text class=card>text</div></section></body>";
template<class Action> void run(const std::string& name, Action action) {
    try { action(); std::printf("HAS_PSEUDO PASS %s\n", name.c_str()); }
    catch (const std::exception& e) {
        ++failures; std::fprintf(stderr,"HAS_PSEUDO FAIL %s: %s\n",name.c_str(),e.what());
    }
}
void one(const std::string& selector, int rules, float opacity) {
    Fixture f(markup,(selector+"{opacity:.8}").c_str(),rules);
    f.resolve(); f.expect("card",opacity); f.expect("empty",.2f); f.expect("text",.2f);
    check(f.resolve()==0,"unchanged resolution without generated content is empty");
    f.expect("card",opacity);
}
void matrix() {
    for (const std::string pseudo : {"::before","::after","::BeFoRe",R"(::b\65 fore)",
                                    R"(::\000061fter)",":/**/:/**/before"}) {
        for (const std::string prefix : {"","span","*","#leaf",".ready","[data-live=yes]",":hover",":first-child"})
            run("direct/"+prefix+pseudo,[&]{one(".card:has("+prefix+pseudo+")",0,.2f);});
        for (int pos=0; pos<3; ++pos) {
            const auto args=pos==0 ? pseudo+",.ready,.absent" :
                pos==1 ? ".ready,"+pseudo+",.absent" : ".ready,.absent,"+pseudo;
            run("strict/"+args,[&]{one(".card:has("+args+")",0,.2f);});
            for (const std::string fn : {"is","where"}) {
                run("recover/"+fn+args,[&]{one(".card:has(:"+fn+"("+args+"))",1,.8f);});
                run("empty/"+fn+pseudo,[&]{one(".card:has(:"+fn+"("+pseudo+"))",1,.2f);});
            }
        }
        run("not-strict/"+pseudo,[&]{one(".card:has(:not(.absent,"+pseudo+"))",0,.2f);});
        run("negated-empty/"+pseudo,[&]{one(".card:has(:not(:is("+pseudo+")))",1,.8f);});
        run("nested-context/"+pseudo,[&]{one(".card:has(:has(.ready)"+pseudo+")",0,.2f);});
        run("outer-is/"+pseudo,[&]{one(".card:is(:has("+pseudo+"),.safe)",1,.8f);});
        run("outer-where/"+pseudo,[&]{one(".card:where(:has("+pseudo+"),.safe)",1,.8f);});
        run("outer-not/"+pseudo,[&]{one(".card:not(:has("+pseudo+"))",0,.2f);});
        run("deep-recovery/"+pseudo,[&]{one(".card:has(:is(:not("+pseudo+"),.ready))",1,.8f);});
        run("deep-has-recovery/"+pseudo,[&]{one(".card:has(:where(:has("+pseudo+"),.ready))",1,.8f);});
        run("top-list/"+pseudo,[&]{one(".card:has("+pseudo+"),.card:has(.ready)",1,.8f);});
    }
    run("literal-string",[]{
        Fixture f("<body><div id=card class=card><span data-x='a,::before)'></span></div></body>",
            ".card:has([data-x='a,::before)']){opacity:.8}",1);
        f.resolve(); f.expect("card",.8f);
    });
    run("escaped-class",[]{
        Fixture f("<body><div id=card class=card><span class='literal::before'></span></div></body>",
            R"(.card:has(.literal\:\:before){opacity:.8})",1);
        f.resolve(); f.expect("card",.8f);
    });
    run("comment-data",[]{one(".card:has(.ready/* ::before , ) */)",1,.8f);});
    run("nested-has-control",[]{
        Fixture f("<body><div id=card class=card><span class=mid><b class=ready></b></span></div></body>",
            ".card:has(.mid:has(.ready)){opacity:.8}",1);
        f.resolve(); f.expect("card",.8f);
    });
    for (const std::string name : {"HAS",R"(\68 as)"})
        run("escaped-has/"+name,[&]{one(".card:"+name+"(::before)",0,.2f);});
}
void cascade() {
    for (const std::string pseudo : {"::before","::after"}) for (int reverse : {0,1}) {
        const std::string ordinary=".card.card{opacity:.4}", low=".card{opacity:.4}";
        const auto discarded=".card:has(:is(#never"+pseudo+",.ready)){opacity:.8}";
        Fixture f(markup,(reverse ? ordinary+discarded : discarded+ordinary).c_str(),2);
        f.resolve(); f.expect("card",reverse ? .8f : .4f);
        const auto impossible=".card:has(:is(#never:is("+pseudo+"),.ready)){opacity:.8}";
        Fixture g(markup,(reverse ? ordinary+impossible : impossible+ordinary).c_str(),2);
        g.resolve(); g.expect("card",.8f);
        for (const auto& branch : {"#never"+pseudo, "#never:is("+pseudo+")"}) {
            const auto zero=".card:has(:where("+branch+",.ready)){opacity:.8}";
            Fixture w(markup,(reverse ? low+zero : zero+low).c_str(),2);
            w.resolve(); w.expect("card",reverse ? .8f : .4f);
        }
    }
}
void mutations() {
    Fixture f(markup,".card:has(:is(::before,#leaf.ready[data-live=yes])){opacity:.6}"
        ".card:has(:where(::after,#leaf.ready[data-live=yes]:hover)){opacity:.8!important}",2);
    auto card=f.find("card"), empty=f.find("empty"), leaf=f.find("leaf");
    check(card && empty && leaf,"mutation targets exist");
    f.resolve(); f.expect("card",.6f);
    for (int round=0; round<3; ++round) {
        for (const char* attr : {"class","id","data-live"}) {
            check(aether_element_set_attribute(leaf,attr,"absent")==AETHER_OK,"disable real matching branch");
            f.resolve(); f.expect("card",.2f);
            const char* value=std::strcmp(attr,"class")==0 ? "ready" : std::strcmp(attr,"id")==0 ? "leaf" : "yes";
            check(aether_element_set_attribute(leaf,attr,value)==AETHER_OK,"restore real matching branch");
            f.resolve(); f.expect("card",.6f);
        }
        check(aether_node_append_child(empty,leaf)==AETHER_OK,"move descendant");
        f.resolve(); f.expect("card",.2f); f.expect("empty",.6f);
        check(aether_node_get_parent(leaf)==empty,"real descendant moved");
        for (int on : {1,0,1,0}) {
            aether_element_set_state(leaf,on ? AETHER_STATE_HOVER : AETHER_STATE_NONE);
            check(f.resolve(leaf)>0,"state change publishes ancestor style");
            f.expect("empty",on ? .8f : .6f); f.expect("card",.2f);
            check(f.changed.count(empty)==1,"state API invalidates HAS subject");
            check(f.changed.at(empty).impact==0x0C,"opacity impact composite/hittest");
            check(f.resolve(leaf)==0,"same state has no updates");
        }
        check(aether_node_append_child(card,leaf)==AETHER_OK,"restore descendant");
        f.resolve(); f.expect("card",.6f); f.expect("empty",.2f);
    }
}
void lifecycle() {
    Fixture f(markup,".card{opacity:.4}",1); f.resolve(); f.expect("card",.4f);
    for (int round=0; round<3; ++round) {
        auto bad=f.add(".card:has(::before){opacity:.99;width:999px}",0);
        auto never=f.add(".card:has(:is(::after)){opacity:.99;width:999px}",1);
        auto good=f.add(".card:has(::after){opacity:.99}.card:has(.ready){opacity:.8}",1);
        check(bad!=never && never!=good && bad!=good,"distinct live stylesheet handles");
        f.resolve(); f.expect("card",.8f); f.dimensions("card",10,11);
        check(f.resolve()==0,"stable sheets no-op");
        check(aether_style_runtime_remove_stylesheet(f.runtime,bad),"remove zero-rule sheet");
        check(!aether_style_runtime_remove_stylesheet(f.runtime,bad),"zero-rule handle removed once");
        f.resolve(); f.expect("card",.8f);
        check(aether_style_runtime_remove_stylesheet(f.runtime,good),"remove valid sheet");
        f.resolve(); f.expect("card",.4f);
        check(aether_style_runtime_remove_stylesheet(f.runtime,never),"remove impossible rule sheet");
        f.resolve(); f.expect("card",.4f); check(f.resolve()==0,"stable removal no-op");
    }
}
void expect_generated(Fixture& f,const char* target,const char* text,float opacity) {
    const auto& values=f.generated[f.find(target)];
    check(values.size()==1,"one generated pseudo update with the expected authored parent");
    check(values[0].text==text,"generated child text");
    check(std::fabs(values[0].opacity-opacity)<.0001f,"generated computed opacity");
    check(std::fabs(values[0].font_size-23.f)<.0001f,"generated inherits host font size");
}
void generated_content() {
    for (const std::string pseudo : {"before","after"}) {
        // Direct rejection is independent of real generated content on hosts or descendants.
        Fixture direct(markup,(".card:has(::"+pseudo+"){opacity:.8}").c_str(),0);
        direct.resolve(); direct.expect("card",.2f);
        auto real=direct.add((".card::"+pseudo+",.ready::"+pseudo+"{content:'X';opacity:.7}").c_str(),2);
        direct.resolve(); direct.expect("card",.2f); direct.expect("empty",.2f);
        for (const char* target : {"card","empty","text","leaf"}) expect_generated(direct,target,"X",.7f);
        check(aether_style_runtime_remove_stylesheet(direct.runtime,real),"remove real generated controls");
        direct.resolve(); direct.expect("card",.2f);

        Fixture only(markup,(".card:has(:not(:is(::"+pseudo+"))){opacity:.8}").c_str(),1);
        auto all=only.add((".card::"+pseudo+"{content:'X';opacity:.7}").c_str(),1);
        only.resolve(); only.expect("card",.8f); only.expect("empty",.2f); only.expect("text",.2f);
        expect_generated(only,"empty","X",.7f);
        check(aether_node_get_child_count(only.find("empty"))==0,"generated-only host has no authored DOM children");
        check(aether_style_runtime_remove_stylesheet(only.runtime,all),"remove generated-only sheet");
        only.resolve(); only.expect("empty",.2f);

        Fixture f(markup,".card:has(:is(::before,.ready)){opacity:.8}",1);
        for (int round=0; round<2; ++round) {
            const auto css=".card:has(.ready)::"+pseudo+"{content:'X';opacity:.7}";
            auto sheet=f.add(css.c_str(),1);
            f.resolve(); f.expect("card",.8f); expect_generated(f,"card","X",.7f);
            // Snapshot text/scalars only. Pseudo handles never survive a resolution.
            f.resolve(); expect_generated(f,"card","X",.7f);
            check(aether_element_set_attribute(f.find("leaf"),"class","absent")==AETHER_OK,"disable outer HAS pseudo");
            f.resolve(); f.expect("card",.2f);
            check(f.generated[f.find("card")].empty(),"no generated update after class disables selector");
            check(aether_element_set_attribute(f.find("leaf"),"class","ready")==AETHER_OK,"restore outer HAS pseudo");
            f.resolve(); f.expect("card",.8f); expect_generated(f,"card","X",.7f);
            for (const std::string content : {"none","normal","''","'Y'"}) {
                auto override_sheet=f.add((".card:has(.ready)::"+pseudo+"{content:"+content+"}").c_str(),1);
                f.resolve(); f.expect("card",.8f);
                if (content=="none" || content=="normal")
                    check(f.generated[f.find("card")].empty(),"suppressed content emits no generated update");
                else expect_generated(f,"card",content=="''" ? "" : "Y",.7f);
                check(aether_style_runtime_remove_stylesheet(f.runtime,override_sheet),"remove content override");
                f.resolve(); expect_generated(f,"card","X",.7f);
            }
            check(aether_style_runtime_remove_stylesheet(f.runtime,sheet),"remove outer pseudo sheet");
            f.resolve(); f.expect("card",.8f);
            check(f.generated[f.find("card")].empty(),"no generated update after sheet removal");
            check(f.resolve()==0,"stable resolution after generated sheet removal");
        }
    }
}
} // namespace
int main() {
    matrix(); run("cascade-ties-and-impossible-branches",cascade);
    run("class-id-attribute-structure-state",mutations); run("sheet-lifecycle",lifecycle);
    run("real-generated-content-and-outer-pseudos",generated_content);
    std::printf("HAS_PSEUDO_ELEMENT FIXTURES=%d RESOLUTIONS=%d ASSERTIONS=%d FAILURES=%d\n",
                fixtures,resolutions,assertions,failures);
    return failures ? 1 : 0;
}
