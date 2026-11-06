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
const char* markup = "<body><section id='ancestor'><div id='123' class='item 123 -1 - -- --1 a1' data-x='123' data-neg='-1' data-dash='-'><a id='leaf' class='leaf'></a></div><div id='control' class='control'></div></section></body>";
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
            add("*{opacity:.2;width:10px;font-size:23px}",1);
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
        check(aether_style_runtime_add_stylesheet(runtime,css.c_str(),2,"identifier starts",&sheet,&rules)==AETHER_OK,
              "add stylesheet status");
        check(sheet != 0,"nonzero sheet including zero-rule sheet");
        check(rules == expected,"exact rule count");
        return sheet;
    }
    int resolve(aether_node_t state_node = nullptr, aether_node_t subtree = nullptr) {
        ++resolutions; changed.clear(); generated.clear();
        aether_style_update_t* updates=nullptr;
        const auto root=subtree ? subtree : reinterpret_cast<aether_node_t>(doc);
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
    try { action(); std::printf("IDENTIFIER_START PASS %s\n",name.c_str()); }
    catch (const std::exception& e) { ++failures; std::fprintf(stderr,"IDENTIFIER_START FAIL %s: %s\n",name.c_str(),e.what()); }
}
void one(const std::string& selector, int rules, bool matches, const char* html=markup) {
    Fixture f(selector+"{opacity:.8}",rules,html);auto target=f.find("123");
    f.resolve();f.expect(target,matches?.8f:.2f);
    check(f.resolve()==0,"second resolution stable");f.expect(target,matches?.8f:.2f);
}
void matrix() {
    struct Case {const char* selector;int rules;bool matches;};
    const Case cases[]={
        {"#123",0,false},{".123",0,false},{".-1",0,false},{".-",0,false},
        {"123",0,false},{"-1",0,false},{"-",0,false},
        {"[123]",0,false},{"[-1]",0,false},{"[-]",0,false},
        {"[data-x=123]",0,false},{"[data-neg=-1]",0,false},{"[data-dash=-]",0,false},
        {R"(#\31 23)",1,true},{R"(.\31 23)",1,true},{R"(.\00003123)",1,true},
        {R"(.\-1)",1,true},{R"(.-\31)",1,true},{R"(.\-)",1,true},
        {".--",1,true},{".--1",1,true},{".a1",1,true},
        {R"([data-x=\31 23])",1,true},{R"([data-neg=\-1])",1,true},
        {R"([data-dash=\-])",1,true},{R"([data-x="123"])",1,true},
        {R"([data-neg="-1"])",1,true},{R"([data-dash="-"])",1,true},
        {".123,.item",1,true},{".item,#123,.-",1,true},
        {":is(#123:hover,.item)",1,true},{":where(#123:hover,.item)",1,true},
        {":is(#123:hover.123,.item)",1,true},
        {R"(:is(.1\,#ghost:hover,.item))",1,true},
        {":is(.123)",1,false},{":where(.123)",1,false},
        {".item:is(.123)",1,false},{":not(.item,.123)",0,false},
        {":has(.leaf,.123)",0,false},{":is(:not(.item,.123),.item)",1,true},
        {":where(:has(.leaf,.123),.item)",1,true},
        {":not(:is(.123))",1,true},{":has(:is(.123))",1,false},
        {":is(#123:is(.123),.item)",1,true}
    };
    for(const auto& c:cases)run(c.selector,[&]{one(c.selector,c.rules,c.matches);});
    for(const std::string token:{"0","1e3","1px","-0","-1e3","-1px","+1",".5","-.5"})
        for(const std::string& selector:{"."+token,"#"+token,token,"["+token+"]","[data-x="+token+"]"})
            run(selector,[&]{one(selector,0,false);});
    for(const std::string name:{"123","-1","-"}) {
        const std::string html="<body><div id='123' class='item' "+name+"='yes'></div></body>";
        const std::string source=name=="123"?R"(\31 23)":(name=="-1"?R"(\-1)":R"(\-)");
        run("attribute name "+name,[&]{one("["+source+"]",1,true,html.c_str());});
    }
    for(const std::string op:{"=","~=","|=","^=","$=","*="}) {
        run("attribute invalid "+op,[&]{one("[data-x"+op+"123]",0,false);});
        run("attribute quoted "+op,[&]{one("[data-x"+op+R"("123"])",1,true);});
        run("attribute escaped "+op,[&]{one("[data-x"+op+R"(\31 23])",1,true);});
    }
}
const char* cdc_markup="<body><div id='--' class='--'><a id='123' class='item'></a></div></body>";
void cdc() {
    for(const std::string selector:{".-->a",".-->a:hover","[-->]","[x=-->]"})
        run("CDC invalid "+selector,[&]{one(selector,0,false,cdc_markup);});
    for(const std::string selector:{"#-->a",".-- > a",".--/**/>a",R"(.-\->a)"})
        run("CDC valid "+selector,[&]{one(selector,1,true,cdc_markup);});
    run("CDC escaped data",[]{one(R"(.--\>a)",1,true,"<body><div id='123' class='item -->a'></div></body>");});
    run("CDC inside name",[]{one(".a-->a",1,true,"<body><div class='a--'><a id='123' class='item'></a></div></body>");});
    run("CDC discarded specificity",[]{
        Fixture f(":is(.-->a,.item){opacity:.8}.item{opacity:.4}",2,cdc_markup);
        f.resolve();f.expect(f.find("123"),.4f);
    });
    run("CDC hash specificity retained",[]{
        Fixture f(":is(#-->a,.item){opacity:.8}.item.item{opacity:.4}",2,cdc_markup);
        f.resolve();f.expect(f.find("123"),.8f);
    });
}
void cascade() {
    for(bool reverse:{false,true}) {
        for(bool escaped:{false,true}) {
            const std::string a=std::string(escaped?R"(:is(#\31 23,.item))":":is(#123,.item)")+"{opacity:.8}";
            const std::string b=".item.item{opacity:.4}";
            Fixture f(reverse?b+a:a+b,2);f.resolve();f.expect(f.find("123"),escaped?.8f:.4f);
        }
        const std::string a=":is(#ghost:hover.123,.item){opacity:.8}",b=".item.item{opacity:.4}";
        Fixture f(reverse?b+a:a+b,2);f.resolve();f.expect(f.find("123"),.4f);
    }
}
void mutations() {
    struct Case {const char* invalid;const char* valid;const char* attr;const char* original;const char* other;bool ancestor;};
    const Case cases[]={
        {"#123",R"(#\31 23)","id","123","other",false},
        {".123",R"(.\31 23)","class","item 123","item",false},
        {"[data-x=123]",R"([data-x="123"])","data-x","123","other",false},
        {"#123 > .leaf",R"(#\31 23 > .leaf)","id","123","other",true},
        {".123 > .leaf",R"(.\31 23 > .leaf)","class","item 123","item",true},
        {"[data-x=123] > .leaf",R"([data-x=\31 23] > .leaf)","data-x","123","other",true}
    };
    for(const auto& c:cases)for(bool valid:{false,true})run(std::string("mutation ")+(valid?c.valid:c.invalid),[&]{
        Fixture f(std::string(valid?c.valid:c.invalid)+"{opacity:.8}",valid?1:0);
        // Give descendants a baseline without overriding the more specific tested selector.
        f.add(".leaf{opacity:.2}",1);
        auto changed=f.find("123"),target=c.ancestor?f.find("leaf"):changed,ancestor=f.find("ancestor");
        check(aether_element_set_attribute(changed,c.attr,c.original)==AETHER_OK,"initial mutation");
        f.resolve();f.expect(target,valid?.8f:.2f);
        for(int round=0;round<3;++round)for(bool restore:{false,true}) {
            check(aether_element_set_attribute(changed,c.attr,restore?c.original:c.other)==AETHER_OK,"mutate attribute");
            // Re-resolve the common ancestor after DOM changes, not the state-only path.
            f.resolve(nullptr,ancestor);f.expect(target,valid&&restore?.8f:.2f);
            check(f.resolve(nullptr,ancestor)==0,"mutated ancestor stable pass");
            f.expect(target,valid&&restore?.8f:.2f);
        }
    });
}
void states() {
    for(const std::string selector:{":is(#123:hover,.absent)",":is(.123:hover,.absent)",
            ":is(#123:hover.123,.absent)",":is(.item:hover[data-x=123],.absent)",
            ":is(.item:hover.-->a,.absent)"})
        run("state rejected "+selector,[&]{
            Fixture f(selector+"{opacity:.8}",1);auto target=f.find("123");f.resolve();f.expect(target,.2f);
            for(bool on:{true,false,true,false}) {
                aether_element_set_state(target,on?AETHER_STATE_HOVER:AETHER_STATE_NONE);
                check(f.resolve(target)==0,"rejected branch cannot add state dependencies");f.expect(target,.2f);
            }
        });
    for(const std::string selector:{R"(:is(#\31 23:hover,.absent))",R"(:is(.\31 23:hover,.absent))",
                                   R"(.item:hover[data-x="123"])"})
        run("state escaped "+selector,[&]{
            Fixture f(selector+"{opacity:.8}",1);auto target=f.find("123");f.resolve();f.expect(target,.2f);
            for(bool on:{true,false,true,false}) {
                aether_element_set_state(target,on?AETHER_STATE_HOVER:AETHER_STATE_NONE);
                check(f.resolve(target)>0,"valid state selector emits change");f.expect_change(target,on?.8f:.2f);
                check(f.resolve(target)==0,"state pass stable");
            }
        });
}
void lifecycle() {
    Fixture f("",0);auto target=f.find("123");f.resolve();f.expect(target,.2f);
    for(int round=0;round<3;++round) {
        const auto invalid=f.add("#123,.123,.-{opacity:.9}",0);
        const auto empty=f.add(":is(.123){opacity:.9}:where(#123){opacity:.9}",2);
        f.resolve();f.expect(target,.2f);
        const auto matching=f.add(R"(#\31 23{opacity:.8})",1);f.resolve();f.expect(target,.8f);
        check(aether_style_runtime_remove_stylesheet(f.runtime,invalid),"remove zero-rule sheet");
        check(aether_style_runtime_remove_stylesheet(f.runtime,empty),"remove empty logical rules");
        f.resolve();f.expect(target,.8f);
        check(aether_style_runtime_remove_stylesheet(f.runtime,matching),"remove valid sheet");
        f.resolve();f.expect(target,.2f);check(f.resolve()==0,"sheet lifecycle stable pass");
    }
}
void generated() {
    Fixture f(R"(.123::before{content:'bad'}
        .item:is(.123)::before{content:'impossible'}
        .item:not(:is(.123))::before{content:'good';opacity:.8})",2);
    auto target=f.find("123");f.resolve();
    check(f.generated[target].size()==1,"only possible generated selector matches");
    check(f.generated[target][0].text=="good","generated content from surviving selector");
    check(std::fabs(f.generated[target][0].opacity-.8f)<.0001f,"generated opacity");
    for(int round=0;round<2;++round) {
        check(aether_element_set_attribute(target,"class","123")==AETHER_OK,"remove generated host class");
        f.resolve();check(f.generated[target].empty(),"generated selector removed");
        check(aether_element_set_attribute(target,"class","item 123")==AETHER_OK,"restore generated host class");
        f.resolve();check(f.generated[target].size()==1,"generated selector restored");
        check(f.generated[target][0].text=="good","generated text restored");
        check(aether_node_get_child_count(target)==1,"authored child count preserved");
    }
}
}
int main() {
    matrix();cdc();run("specificity and order",cascade);mutations();states();
    run("add remove re-add",lifecycle);run("generated lifecycle",generated);
    std::printf("selector identifier start runtime: %d fixtures, %d resolutions, %d checks, %d failures\n",
                fixtures,resolutions,assertions,failures);
    return failures?1:0;
}
