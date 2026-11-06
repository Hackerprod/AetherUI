#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// Public ABI regression. Every check and side effect remains active with NDEBUG.
namespace {
int assertions = 0, fixtures = 0, resolutions = 0, failures = 0;
void check(bool ok, const char* message) {
    ++assertions;
    if (!ok) throw std::runtime_error(message);
}
struct Values { float opacity, width; uint32_t impact; };
struct Generated { std::string text; float opacity, font_size; };
const char* markup = "<body><section id=ancestor class=ancestor><i id=previous></i>"
    "<div id=target class='item ready' data-live=yes data-x='#target#target'>"
    "<span id=leaf class=ready></span></div><div id=empty class=item></div></section></body>";
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    std::set<aether_node_t> authored;
    std::map<aether_node_t, Values> styles, changed;
    std::map<aether_node_t, std::vector<Generated>> generated;
    explicit Fixture(const std::string& css, int rules, const char* html = markup) {
        ++fixtures;
        try {
            check(aether_doc_create(&doc) == AETHER_OK, "create document");
            check(aether_doc_parse_html(doc, html) == AETHER_OK, "parse HTML");
            collect(reinterpret_cast<aether_node_t>(doc));
            aether_style_runtime_options_t options{};
            const aether_style_environment_t env{800,600,1,16,0,0,0};
            check(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime) == AETHER_OK,
                  "create runtime");
            add(".item{opacity:.2;width:10px;font-size:23px}",1);
            add(css,rules);
        } catch (...) { cleanup(); throw; }
    }
    ~Fixture() { cleanup(); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    void cleanup() {
        if (runtime) aether_style_runtime_destroy(runtime);
        if (doc) aether_doc_destroy(doc);
        runtime=nullptr; doc=nullptr;
    }
    void collect(aether_node_t node) {
        authored.insert(node);
        for (int i=0; i<aether_node_get_child_count(node); ++i) collect(aether_node_get_child(node,i));
    }
    aether_node_t find(const char* id) {
        auto node=aether_doc_get_element_by_id(doc,id);
        check(node != nullptr,"fixture node exists");
        return node;
    }
    int64_t add(const std::string& css, int expected) {
        int64_t sheet=0; int32_t rules=-1;
        check(aether_style_runtime_add_stylesheet(runtime,css.c_str(),2,"multiple IDs",&sheet,&rules)==AETHER_OK,
              "add stylesheet status");
        check(sheet != 0,"nonzero sheet including zero-rule sheet");
        check(rules == expected,"exact rule count");
        return sheet;
    }
    int resolve(aether_node_t state_node = nullptr) {
        ++resolutions; changed.clear(); generated.clear();
        aether_style_update_t* updates=nullptr;
        const auto root=reinterpret_cast<aether_node_t>(doc);
        const int count=state_node
            ? aether_style_runtime_resolve_state_changes(runtime,root,&state_node,1,&updates)
            : aether_style_runtime_resolve_subtree(runtime,root,&updates);
        try {
            check(count >= 0,"resolve status");
            check((count == 0) == (updates == nullptr),"update pointer/count contract");
            for (int i=0; i<count; ++i) {
                aether_node_t node=nullptr; aether_computed_style_handle_t handle=nullptr;
                uint32_t impact=0;
                const auto status=aether_style_runtime_get_update(updates[i],&node,&handle,&impact);
                if (status != AETHER_OK || !handle || !node) {
                    if (handle) aether_computed_style_handle_destroy(handle);
                    check(false,"read style update");
                }
                aether_computed_style_t style{};
                const auto copied=aether_computed_style_copy(handle,&style);
                aether_computed_style_handle_destroy(handle);
                check(copied == AETHER_OK,"copy style update");
                const Values value{style.opacity,style.width.value,impact};
                const float font=style.font_size;
                aether_computed_style_free(&style);
                if (authored.count(node)) { styles[node]=value; changed[node]=value; }
                else {
                    const auto parent=aether_node_get_parent(node);
                    check(authored.count(parent)==1,"pseudo parent is an authored host");
                    check(aether_node_get_child_count(node)==1,"generated content has one text child");
                    const auto child=aether_node_get_child(node,0);
                    check(child && aether_node_get_type(child)==AETHER_NODE_TEXT &&
                          aether_node_get_parent(child)==node,"generated text parent/type");
                    char* text=aether_node_get_text_content(child);
                    check(text != nullptr,"read generated content");
                    const std::string content=text; aether_string_free(text);
                    generated[parent].push_back({content,value.opacity,font});
                }
            }
        } catch (...) { aether_style_updates_free(updates,count); throw; }
        aether_style_updates_free(updates,count);
        return count;
    }
    void expect(aether_node_t node, float opacity) {
        // The public map getter is currently a stub. Keep owned scalar values
        // from public updates; an unchanged pass need not emit another snapshot.
        check(styles.count(node)==1,"node has a resolved scalar snapshot");
        check(std::fabs(styles.at(node).opacity-opacity)<.0001f,"computed opacity");
    }
    void expect_change(aether_node_t node, float opacity) {
        check(changed.count(node)==1,"affected subject emitted a fresh style update");
        expect(node,opacity);
    }
    void set_id(aether_node_t node, const char* id) {
        check(aether_element_set_attribute(node,"id",id)==AETHER_OK,"mutate ID");
    }
};
template<class F> void run(const std::string& name, F action) {
    try { action(); std::printf("MULTIPLE_IDS PASS %s\n",name.c_str()); }
    catch (const std::exception& e) { ++failures; std::fprintf(stderr,"MULTIPLE_IDS FAIL %s: %s\n",name.c_str(),e.what()); }
}
void one(const std::string& selector, int rules, bool matches) {
    Fixture f(selector+"{opacity:.8}",rules); auto target=f.find("target");
    f.resolve(); f.expect(target,matches ? .8f : .2f);
    check(f.resolve()==0,"unchanged subtree is a no-op"); f.expect(target,matches ? .8f : .2f);
}
void matrix() {
    struct Case { const char* text; bool matches; int rules=1; };
    const Case cases[]={
        {"#target#target",true},{"#target#target#target",true},
        {"#target#other",false},{"#other#target",false},{"#target#other#target",false},
        {"#target#target#other",false},{"#target#Target",false},{"#Target#target",false},
        {"div#target.item#target",true},{"#target[data-live=yes]#target",true},
        {"#target:enabled#target",true},{"#target/**/#target",true},
        {R"(#target#tar\67 et)",true},{"#target/**/ #target",false},
        {"#ancestor#ancestor > #target#target",true},
        {"#ancestor#wrong > #target#target",false},
        {"#previous#previous + #target#target",true},
        {"#previous#wrong + #target#target",false},
        {"#previous#previous ~ .item",true},
        {":is(#target#target,.absent)",true},{".item:where(#target#target)",true},
        {":is(#target#other,.ready)",true},{":not(#target#other)",true},
        {":not(#target#target)",false},{":not(:is(#target#target))",false},
        {":not(:where(#target#target))",false},{":not(:not(#target#target))",true},
        {":is(#target#target,:unknown)",true},{".item:where(#target#target,:unknown)",true},
        {":not(#target#target,:unknown)",false,0},{":not(#target#other:unknown)",false,0},
        {":is(#target#other:unknown,.ready)",true},
        {":is(#target#other:unknown)",false},{":where(#target#other:unknown)",false},
        {".item:has(#leaf#leaf)",true},{".item:has(#leaf#wrong)",false},
        {".item:has(#leaf#wrong,.ready)",true},{".item:has(:not(#leaf#wrong))",true},
        {".item:has(:not(#leaf#leaf))",false},
        {".item:has(:is(#leaf#leaf,::before))",true},
        {".item:has(#leaf#leaf,::before)",false,0},
        {".item:has(#leaf#leaf:unknown)",false,0},
        {".item:has(:is(#leaf#leaf:unknown))",false},
        {"#target#",false,0},{"#target##other",false,0},{"#target#other:unknown",false,0},
        {"#target#target\\\n",false,0},
        {R"([data-x="#target#target"])",true},
        {"#target#other,.item",true,2},{"#target#target,.item",true,2}
    };
    for (const auto& c: cases) run(c.text,[&]{one(c.text,c.rules,c.matches);});
}
void cascade() {
    const std::vector<std::string> high={"#target#target",":is(#never#other,.item)",
        ":not(#never#other)",".item:has(#never#other,.ready)",
        ".item:has(:is(#never#other,.ready))"};
    for (const auto& selector:high) for (bool reverse:{false,true}) {
        const auto a=selector+"{opacity:.8}",b=std::string("#target{opacity:.4}");
        Fixture f(reverse ? b+a : a+b,2); f.resolve(); f.expect(f.find("target"),.8f);
    }
    for (bool reverse:{false,true}) {
        const std::string a=":where(#never#other,.item){opacity:.8}",b=".item{opacity:.4}";
        Fixture zero(reverse ? b+a : a+b,2); zero.resolve(); zero.expect(zero.find("target"),.4f);
        const std::string first="#target#target{opacity:.6}",second=R"(#target#tar\67 et{opacity:.9})";
        Fixture tie(reverse ? second+first : first+second,2); tie.resolve();
        tie.expect(tie.find("target"),reverse ? .6f : .9f);
    }
    Fixture important("#target#target{opacity:.8}#target{opacity:.4!important}",2);
    important.resolve(); important.expect(important.find("target"),.4f);
}
void mutation_and_sheets() {
    struct Case { const char* selector; const char* changed; };
    for (const auto& c:std::vector<Case>{{"#target#target","target"},
            {"#ancestor#ancestor > .item","ancestor"},
            {"#previous#previous + .item","previous"},
            {".item:has(#leaf#leaf)","leaf"},
            {".item:is(#target#target)","target"},
            {".item:not(#wrong#wrong)","target"}}) {
        Fixture f(std::string(c.selector)+"{opacity:.8}",1);
        auto target=f.find("target"), changed=f.find(c.changed);
        f.resolve(); f.expect(target,.8f);
        for (int round=0; round<3; ++round) {
            f.set_id(changed,"wrong"); f.resolve(); f.expect_change(target,.2f);
            check(f.resolve()==0,"mutated ID stable pass"); f.expect(target,.2f);
            f.set_id(changed,c.changed); f.resolve(); f.expect_change(target,.8f);
            check(f.resolve()==0,"restored ID stable pass"); f.expect(target,.8f);
        }
    }
    Fixture f("#target#target{opacity:.4}",1); auto target=f.find("target");
    f.resolve(); f.expect(target,.4f);
    for (int round=0; round<3; ++round) {
        const auto impossible=f.add("#target#wrong{opacity:.99}",1);
        const auto matching=f.add("#target#target{opacity:.8}",1);
        const auto invalid=f.add("#target##wrong{opacity:.99}",0);
        f.resolve(); f.expect(target,.8f);
        check(aether_style_runtime_remove_stylesheet(f.runtime,impossible),"remove impossible sheet");
        f.resolve(); f.expect(target,.8f);
        check(aether_style_runtime_remove_stylesheet(f.runtime,matching),"remove matching sheet");
        f.resolve(); f.expect_change(target,.4f);
        check(aether_style_runtime_remove_stylesheet(f.runtime,invalid),"remove zero-rule sheet");
        f.resolve(); f.expect(target,.4f);
        check(f.resolve()==0,"rebuilt index stable pass");
    }
}
void states() {
    struct State { const char* name; int bit; };
    for (const auto state: std::vector<State>{{"hover",AETHER_STATE_HOVER},{"focus",AETHER_STATE_FOCUS},
                {"active",AETHER_STATE_ACTIVE},{"invalid",AETHER_STATE_INVALID}}) {
        const std::string pseudo=std::string(":")+state.name;
        const std::vector<std::pair<std::string,std::string>> contexts={
            {"#target"+pseudo+"#target","target"},
            {".item:is(#target#target"+pseudo+")","target"},
            {"#ancestor#ancestor"+pseudo+" .item","ancestor"},
            {"#previous#previous"+pseudo+" + .item","previous"},
            {".item:has(#leaf#leaf"+pseudo+")","leaf"},
            // Keep the impossible branch's nonlocal state metadata too.
            {".item:has(:is(#leaf#wrong"+pseudo+",.ready"+pseudo+"))","leaf"}};
        for (const auto& c:contexts) {
            Fixture f(c.first+"{opacity:.8}",1); auto target=f.find("target"), changed=f.find(c.second.c_str());
            f.resolve(); f.expect(target,.2f);
            for (bool on:{true,false,true,false}) {
                aether_element_set_state(changed,on ? state.bit : AETHER_STATE_NONE);
                check(f.resolve(changed)>0,"state change publishes updates");
                f.expect_change(target,on ? .8f : .2f);
                check(f.changed.at(target).impact==0x0C,"opacity impact composite/hittest");
                check(f.resolve(changed)==0,"unchanged incremental state is a no-op");
            }
        }
    }
}
void generated_content() {
    for (const std::string pseudo:{"before","after"}) {
        const std::string css=".item#target#target::"+pseudo+"{content:'X';opacity:.8}"
            ".item:has(#target#target){width:999px}";
        Fixture f(css,2); auto target=f.find("target");
        for (int pass=0; pass<6; ++pass) {
            const bool matches=pass%3!=1;
            f.set_id(target,matches ? "target" : "wrong");
            f.resolve(); f.expect(target,.2f);
            check(f.styles.at(target).width==10,"HAS ignores generated nodes carrying host ID");
            check(aether_node_get_child_count(target)==1,"generated node absent from authored DOM children");
            const auto& generated=f.generated[target];
            check(generated.size()==(matches ? 1u : 0u),"generated appearance/disappearance");
            if (matches) {
                check(generated[0].text=="X","real generated text");
                check(std::fabs(generated[0].opacity-.8f)<.0001f,"generated multi-ID style");
                check(std::fabs(generated[0].font_size-23)<.0001f,"generated inherits host font");
            }
        }
        for (const std::string impossible:{"#target#wrong","#wrong#target"}) {
            Fixture absent(impossible+"::"+pseudo+"{content:'X';opacity:.8}",1);
            absent.resolve(); check(absent.generated.empty(),"contradictory pseudo prefix never generates");
        }
    }
}
void identity_and_sizes() {
    for (const auto& id:std::vector<std::string>{"target","Target","caf\xc3\xa9",std::string(256,'x')}) {
        const std::string html="<body><div id='"+id+"' class=item></div></body>";
        for (int count:{0,1,2,3,32,1024}) {
            std::string selector=".item";
            for (int i=0;i<count;++i) selector+="#"+id;
            Fixture f(selector+"{opacity:.8}",1,html.c_str()); auto target=f.find(id.c_str());
            f.resolve(); f.expect(target,.8f);
            f.set_id(target,"absent"); f.resolve(); f.expect(target,count ? .2f : .8f);
            f.set_id(target,id.c_str()); f.resolve(); f.expect(target,.8f);
        }
    }
    for (const auto& spelling:std::vector<std::pair<std::string,std::string>>{
            {"hash#id",R"(#hash\#id#hash\23 id)"},
            {"comma,id",R"(#comma\,id#comma\2c id)"},
            {"plus+id",R"(#plus\+id#plus\2b id)"}}) {
        const std::string html="<body><div id='"+spelling.first+"' class=item></div></body>";
        Fixture f(spelling.second+"{opacity:.8}",1,html.c_str());
        f.resolve(); f.expect(f.find(spelling.first.c_str()),.8f);
    }
}
}
int main() {
    matrix(); run("cascade",cascade); run("ID mutations and stylesheet lifecycle",mutation_and_sheets);
    run("incremental state dependencies",states); run("generated content and origin",generated_content);
    run("decoded identity and sizes",identity_and_sizes);
    std::printf("MULTIPLE_IDS SUMMARY assertions=%d fixtures=%d resolutions=%d failures=%d\n",
                assertions,fixtures,resolutions,failures);
    return failures ? 1 : 0;
}
