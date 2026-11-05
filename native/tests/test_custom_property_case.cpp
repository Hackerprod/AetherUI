#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Public ABI only. Checks deliberately remain active in Release/NDEBUG builds.
static std::string phase;
static int checks = 0;
static void require(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL [%s]: %s\n", phase.c_str(), message);
        std::exit(1);
    }
}
struct snapshot {
    aether_computed_style_t value{};
    ~snapshot() { aether_computed_style_free(&value); }
};
struct fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_node_t body = nullptr;
    int64_t sheet = 0;
    std::map<aether_node_t, std::unique_ptr<snapshot>> styles;
    std::vector<aether_node_t> changed;
    fixture(const std::string& css, const char* markup = "<body><div id='target'></div></body>") {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc, markup) == AETHER_OK, "parse HTML");
        body = aether_doc_get_body(doc);
        require(body != nullptr, "body exists");
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK, "create runtime");
        add(css);
    }
    ~fixture() { styles.clear(); aether_style_runtime_destroy(runtime); aether_doc_destroy(doc); }
    void add(const std::string& css) {
        require(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 2, "custom-property-case.css", &sheet, nullptr) == AETHER_OK, "add sheet");
    }
    aether_node_t id(const char* name) {
        auto node = aether_doc_get_element_by_id(doc, name);
        require(node != nullptr, "node exists");
        return node;
    }
    void inline_css(const char* name, const std::string& css) {
        require(aether_element_set_attribute(id(name), "style", css.c_str()) == AETHER_OK, "set inline style");
    }
    void collect(aether_style_update_t* updates, int count) {
        require(count >= 0, "resolve succeeds");
        changed.clear();
        for (int i = 0; i < count; ++i) {
            aether_node_t node = nullptr;
            aether_computed_style_handle_t handle = nullptr;
            require(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr) == AETHER_OK, "get update");
            auto copy = std::make_unique<snapshot>();
            require(aether_computed_style_copy(handle, &copy->value) == AETHER_OK, "copy public style");
            aether_computed_style_handle_destroy(handle);
            styles[node] = std::move(copy);
            changed.push_back(node);
        }
        aether_style_updates_free(updates, count);
    }
    void resolve(const char* incremental = nullptr) {
        aether_style_update_t* updates = nullptr;
        int count;
        if (incremental) {
            auto node = id(incremental);
            count = aether_style_runtime_resolve_state_changes(runtime, body, &node, 1, &updates);
        } else count = aether_style_runtime_resolve_subtree(runtime, body, &updates);
        collect(updates, count);
    }
    void viewport(float width) {
        aether_style_environment_t env{width, 600, 1, 16, 0, 0, 0};
        aether_style_update_t* updates = nullptr;
        int count = aether_style_runtime_set_environment(runtime, &env, body, &updates);
        collect(updates, count);
    }
    const aether_computed_style_t& style(const char* name) {
        auto found = styles.find(id(name));
        require(found != styles.end(), "style snapshot exists");
        return found->second->value;
    }
    void size(const char* name, float width, float height) {
        const auto& s = style(name);
        if (s.width.unit != AETHER_CSS_UNIT_PX || s.height.unit != AETHER_CSS_UNIT_PX ||
            std::fabs(s.width.value - width) > .001f || std::fabs(s.height.value - height) > .001f) {
            std::fprintf(stderr, "node %s got %g (unit %d) x %g (unit %d), expected %g x %g\n",
                         name, s.width.value, int(s.width.unit), s.height.value, int(s.height.unit), width, height);
            require(false, "computed dimensions");
        }
        require(true, "computed dimensions");
    }
};
static void both(const std::string& name, const std::string& declarations, float width = 37, float height = 1) {
    for (bool inlined : {false, true}) {
        phase = name + (inlined ? " / inline" : " / sheet");
        fixture f(inlined ? "" : "#target{" + declarations + "}");
        if (inlined) f.inline_css("target", declarations);
        f.resolve();
        f.size("target", width, height);
    }
}
static void identity() {
    both("exact mixed", "--Brand:37px;width:var(--Brand,11px);height:1px");
    both("wrong case fallback", "--Brand:37px;width:var(--brand,11px);height:1px", 11);
    both("lower then upper", "--brand:23px;--Brand:37px;width:var(--brand);height:var(--Brand)",23,37);
    both("upper then lower", "--Brand:37px;--brand:23px;width:var(--brand);height:var(--Brand)",23,37);
    both("same key order", "--Brand:17px;--brand:23px;--Brand:37px;width:var(--brand);height:var(--Brand)",23,37);
    both("importance isolated", "--brand:23px;--Brand:37px!important;--Brand:19px;width:var(--brand);height:var(--Brand)",23,37);
    for (const char* function : {"var", "VAR", "vAr", "v\\61r"})
        both(std::string("function ") + function, std::string("--Brand:37px;WIDTH:") + function + "(--Brand);HEIGHT:1px");
    both("standard escaped", "--Brand:37px;\\57 IDTH:var(--Brand);HEIGHT:1px");
    both("alias distinct case", "--A:var(--a);--a:37px;width:var(--A,11px);height:1px");
    both("mixed alias", "--Brand:37px;--Alias:var(--Brand);width:var(--Alias);height:1px");
    // Existing resolver policy allows the consumer's fallback when the referenced chain cycles.
    both("real two-key cycle", "--A:var(--a);--a:var(--A);width:var(--A,11px);height:1px",11);
    both("decoded self cycle", "--A:var(--\\41);width:var(--A,11px);height:1px",11);
    for (bool inlined : {false,true}) {
        phase = "wrong case without fallback invalidates winning width";
        std::string css = "--Brand:37px;width:99px;width:var(--brand);height:1px";
        fixture f(inlined ? "" : "#target{" + css + "}");
        if (inlined) f.inline_css("target",css);
        f.resolve();
        require(f.style("target").width.unit == AETHER_CSS_UNIT_AUTO, "invalid winning width is AUTO, not prior width");
    }
    phase = "uppercase shorthand";
    fixture f("#target{--Gap:7px;MARGIN:var(--Gap);width:37px;height:1px}");
    f.resolve();
    require(f.style("target").margin.top.unit == AETHER_CSS_UNIT_PX && f.style("target").margin.top.value == 7, "MARGIN var shorthand");
}
static void escapes() {
    for (const char* declaration : {"--\\6A", "--\\6a", "--j", "--\\00006a"})
        for (const char* reference : {"--\\6A", "--\\6a", "--j", "--\\00006a"})
            both(std::string("escape equivalence ") + declaration + " / " + reference,
                 std::string(declaration) + ":37px;width:var(" + reference + ",11px);height:1px");
    both("J separate from j", "--\\4A:37px;--\\6a:23px;width:var(--J);height:var(--j)",37,23);
    for (const char* ref : {"--A", "--\\41"})
        both("escaped uppercase versus lowercase", std::string("--\\41:37px;--a:23px;width:var(")+ref+");height:var(--\\61)",37,23);
    both("simple nonhex escape", "--\\G:37px;--g:23px;width:var(--G);height:var(--g)",37,23);
    both("literal then escaped winner", "--j:17px;--\\6A:37px;width:var(--j);height:1px");
    both("escaped then literal winner", "--\\6A:17px;--j:37px;width:var(--\\6a);height:1px");
    both("literal importance winner", "--j:37px!important;--\\6A:17px;width:var(--\\6a);height:1px");
    both("escaped importance winner", "--\\6A:37px!important;--j:17px;width:var(--j);height:1px");
    for (const char* name : {"\\2d\\2d Brand", "--Brand", "-\\2d Brand"})
        both("escaped dashed prefix", std::string(name)+":37px;width:var(\\2d\\2d Brand,11px);height:1px");
    both("bare dashed declaration rejected", "--:99px;--Good:37px;width:var(--Good);height:1px");
    phase = "bare dashed reference rejected";
    fixture invalid_name("#target{--:37px;width:var(--,11px)}");
    invalid_name.resolve();
    require(invalid_name.style("target").width.unit == AETHER_CSS_UNIT_AUTO,"bare dashed var invocation invalid");
    for (const char* term : {" ", "\t", "\n", "\r", "\f", "\r\n"}) {
        both("hex terminator", std::string("--\\41")+term+"z:37px;width:var(--Az);height:1px");
        both("hex terminator in reference", std::string("--Az:37px;width:var(--\\41")+term+"z);height:1px");
        both("second whitespace not swallowed", std::string("--Az:37px;--\\41")+term+" z:99px;width:var(--Az);height:1px");
    }
    both("six digits followed by letter", "--\\000041z:37px;width:var(--Az);height:1px");
    both("escaped final space", "--x\\ :37px;--x:23px;width:var(--x\\20 );height:var(--x)",37,23);
    both("hex final space", "--x\\20 :37px;width:var(--x\\ ,11px);height:1px");
    const char* punctuation[][2] = {{"\\:","\\3a "},{"\\;","\\3b "},{"\\,","\\2c "},{"\\)","\\29 "},{"\\{","\\7b "},{"\\}","\\7d "},{"\\\\","\\5c "}};
    for (const auto& p : punctuation) {
        both(std::string("punctuation ")+p[0], std::string("--x")+p[0]+":37px;--next:1px;width:var(--x"+p[1]+",11px);height:var(--next)");
        both(std::string("reverse punctuation ")+p[0], std::string("--x")+p[1]+":37px;--next:1px;width:var(--x"+p[0]+",11px);height:var(--next)");
    }
    both("Unicode literal to hex", "--é:37px;width:var(--\\e9);height:1px");
    both("Unicode hex to literal", "--\\e9:37px;width:var(--é);height:1px");
    both("Unicode simple escape", "--\\é:37px;width:var(--é);height:1px");
    both("non BMP", "--\\1f680:37px;width:var(--🚀);height:1px");
    both("non BMP reverse", "--🚀:37px;width:var(--\\1F680);height:1px");
    both("Unicode no casefold", "--É:37px;--é:23px;width:var(--É);height:var(--é)",37,23);
    both("Unicode no normalization", "--é:37px;--é:23px;width:var(--é);height:var(--é)",37,23);
    for (const char* escape : {"\\0", "\\d800", "\\110000"})
        both("invalid scalar replacement", std::string("--")+escape+":37px;width:var(--�);height:1px");
    both("extra token rejected", "--Bad extra:99px;--Good:37px;width:var(--Good);height:var(--Bad,1px)");
    for (const char* newline : {"\n","\r","\f"})
        both("escape newline rejected", std::string("--Bad\\")+newline+":99px;--Good:37px;width:var(--Good);height:var(--Bad,1px)");
    both("malformed UTF8 rejected", std::string("--Bad")+char(0xc0)+char(0xaf)+":99px;--Good:37px;width:var(--Good);height:var(--Bad,1px)");
}
static const char* tree = "<body><div id='ancestor'><div id='middle'><div id='child'><div id='grandchild'></div></div></div></div></body>";
static std::string consumers = "#child,#grandchild{width:var(--Brand,11px);height:var(--brand,13px)}";
static void inherited(fixture& f, float upper, float lower = 23) {
    f.size("child",upper,lower); f.size("grandchild",upper,lower);
}
static void inheritance_and_cascade() {
    phase = "inheritance through two generations";
    fixture f("#ancestor{--Brand:37px;--brand:23px}" + consumers, tree);
    f.resolve(); inherited(f,37);
    f.inline_css("child","--Brand:59px"); f.resolve(); inherited(f,59);
    f.inline_css("child","--brand:29px"); f.resolve(); inherited(f,37,29);
    for (const char* keyword : {"inherit", "unset"}) {
        phase = std::string("exact keyword ")+keyword;
        f.inline_css("child",std::string("--Brand:")+keyword+";--brand:29px"); f.resolve(); inherited(f,37,29);
    }
    phase = "initial isolates exact key";
    f.inline_css("child","--Brand:initial;--brand:29px"); f.resolve(); inherited(f,11,29);
    f.inline_css("child",""); f.resolve(); inherited(f,37);
    phase = "specificity and inline identity";
    fixture cascade("div{--Brand:19px;--brand:23px}#target{--Brand:37px!important;width:var(--Brand);height:var(--brand)}");
    cascade.inline_css("target","--Brand:59px;--brand:29px"); cascade.resolve(); cascade.size("target",37,29);
    phase = "inherited escaped identity";
    fixture escaped("#ancestor{--\\6a:37px;--J:23px}#child{--j:59px}#child,#grandchild{width:var(--\\6A);height:var(--J)}",tree);
    escaped.resolve(); inherited(escaped,59);
}
static void recomputation() {
    phase = "inline custom-only ancestor and no-op middle";
    fixture f(consumers,tree);
    f.inline_css("ancestor","--Brand:37px;--brand:23px"); f.resolve(); inherited(f,37);
    for (int pass=0;pass<8;++pass) {
        float value = pass%2 ? 37 : 59;
        f.inline_css("ancestor",std::string("--Brand:")+(pass%2 ? "37" : "59")+"px;--brand:23px");
        f.resolve(); inherited(f,value);
        for (auto node : f.changed) require(node != f.id("ancestor") && node != f.id("middle"), "custom-only mutation has no spurious visual ancestor update");
    }
    f.resolve(); require(f.changed.empty(), "identical recompute emits no updates");
    f.inline_css("ancestor","--brand:23px"); f.resolve(); inherited(f,11);
    f.inline_css("ancestor","--Brand:37px;--brand:23px"); f.inline_css("child","--Brand:59px"); f.resolve(); inherited(f,59);
    f.inline_css("child",""); f.resolve(); inherited(f,37);
    phase = "replacement ancestor state and incremental descendant";
    f.inline_css("ancestor","--Brand:59px;--brand:23px;width:123px"); f.resolve(); inherited(f,59);
    f.inline_css("child","width:var(--Brand);height:var(--brand);padding:2px");
    f.resolve("child"); inherited(f,59);
    phase = "incremental hover ancestor custom-only";
    fixture hover("#ancestor{--Brand:37px;--brand:23px}#ancestor:hover{--Brand:59px}"+consumers,tree);
    hover.resolve(); inherited(hover,37);
    for (bool on : {true,false,true,false}) {
        aether_element_set_state(hover.id("ancestor"),on ? AETHER_STATE_HOVER : AETHER_STATE_NONE);
        hover.resolve("ancestor"); inherited(hover,on ? 59 : 37);
    }
    phase = "media ancestor custom-only";
    fixture media("#ancestor{--Brand:37px;--brand:23px}@media(min-width:900px){#ancestor{--Brand:59px}}"+consumers,tree);
    media.resolve(); inherited(media,37);
    media.viewport(1000); inherited(media,59); media.viewport(800); inherited(media,37);
    phase = "replace sheet then inline mutation";
    require(aether_style_runtime_remove_stylesheet(f.runtime,f.sheet) == AETHER_TRUE,"remove sheet");
    f.inline_css("ancestor",""); f.inline_css("child","");
    f.add("#ancestor{--Brand:71px;--brand:23px}"+consumers); f.resolve(); inherited(f,71);
    f.inline_css("ancestor","--Brand:59px"); f.resolve(); inherited(f,59);
    phase = "spelling-only ancestor mutation";
    fixture spelling("#child,#grandchild{width:var(--j);height:var(--J)}",tree);
    spelling.inline_css("ancestor","--j:37px;--J:23px"); spelling.resolve(); inherited(spelling,37);
    spelling.inline_css("ancestor","--\\6A:37px;--J:23px"); spelling.resolve(); inherited(spelling,37);
    require(spelling.changed.empty(),"equivalent spelling emits no changes");
    spelling.inline_css("ancestor","--\\6a:59px;--J:23px"); spelling.resolve(); inherited(spelling,59);
    spelling.inline_css("ancestor","--j:37px;--J:23px"); spelling.resolve(); inherited(spelling,37);
}
static void value_and_snapshot_lifetime() {
    phase = "uppercase CONTENT and preserved value casing";
    fixture pseudo("#target{--Label:\"MiXeD\"}#target::before{CONTENT:var(--Label)}");
    pseudo.resolve();
    bool found_content = false;
    for (auto node : pseudo.changed) {
        if (node == pseudo.body || node == pseudo.id("target")) continue;
        for (int i=0; i<aether_node_get_child_count(node); ++i) {
            char* text = aether_node_get_text_content(aether_node_get_child(node,i));
            found_content = found_content || (text && std::strcmp(text,"MiXeD") == 0);
            aether_string_free(text);
        }
    }
    require(found_content,"generated custom content retains case");
    phase = "public update handle and copied dynamic buffers lifetime";
    fixture f("#target{--Brand:37px;width:var(--Brand);background-image:url('asset://x'),linear-gradient(90deg,red,blue)}");
    aether_style_update_t* updates = nullptr;
    int count = aether_style_runtime_resolve_subtree(f.runtime,f.body,&updates);
    require(count > 0,"initial snapshots");
    aether_computed_style_handle_t retained = nullptr;
    for(int i=0;i<count;++i) {
        aether_node_t node = nullptr; aether_computed_style_handle_t handle = nullptr;
        require(aether_style_runtime_get_update(updates[i],&node,&handle,nullptr)==AETHER_OK,"capture handle");
        if(node==f.id("target")) retained=handle; else aether_computed_style_handle_destroy(handle);
    }
    require(retained!=nullptr,"target snapshot handle");
    snapshot old;
    require(aether_computed_style_copy(retained,&old.value)==AETHER_OK,"copy retained style");
    aether_style_updates_free(updates,count);
    for(int i=0;i<4;++i) {
        f.inline_css("target", i%2 ? "--Brand:37px" : "--Brand:59px"); f.resolve(); f.resolve();
    }
    snapshot after;
    require(aether_computed_style_copy(retained,&after.value)==AETHER_OK,"handle survives updates release and recompute");
    for(const auto* s : {&old.value,&after.value}) {
        require(s->width.unit==AETHER_CSS_UNIT_PX && s->width.value==37,"old width immutable");
        require(s->background_layers.count==2 && s->background_layers.items,"old dynamic background layers");
        require(s->background_layers.items[0].image_source && std::strcmp(s->background_layers.items[0].image_source,"asset://x")==0,"old URL buffer");
        require(s->background_layers.items[1].gradient.stop_count==2 && s->background_layers.items[1].gradient.stops,"old gradient buffer");
    }
    aether_computed_style_handle_destroy(retained);
}
int main(int argc, char** argv) {
    phase="ABI";
    require(aether_abi_version()==AETHER_ABI_VERSION_CURRENT,"ABI version");
    require((aether_abi_capabilities() & (AETHER_CAPABILITY_DOM|AETHER_CAPABILITY_STYLE_RUNTIME)) == (AETHER_CAPABILITY_DOM|AETHER_CAPABILITY_STYLE_RUNTIME),"required capabilities");
    const std::string group = argc > 1 ? argv[1] : "all";
    require(group == "all" || group == "identity" || group == "escapes" || group == "inheritance" || group == "recompute" || group == "lifetime", "known test group");
    if (group == "all" || group == "identity") identity();
    if (group == "all" || group == "escapes") escapes();
    if (group == "all" || group == "inheritance") inheritance_and_cascade();
    if (group == "all" || group == "recompute") recomputation();
    if (group == "all" || group == "lifetime") value_and_snapshot_lifetime();
    std::printf("OK: custom property decoded case-sensitive identity (%d checks)\n",checks);
    return 0;
}
