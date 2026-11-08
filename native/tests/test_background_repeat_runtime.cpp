// Public ABI coverage of the contiguous repeat unit and atomic shorthand cascade.
// Every operation/check remains live in Release builds with NDEBUG.
#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace {
int checks = 0;
void require(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL background-repeat runtime: %s\n", message); std::exit(1); }
}
bool near(float a, float b) { return std::fabs(a-b) < .0001f; }
const aether_color_rgba black{0,0,0,1}, white{1,1,1,1}, red{1,0,0,1},
    blue{0,0,1,1}, yellow{1,1,0,1}, purple{102/255.f,51/255.f,153/255.f,1}, clear{0,0,0,0};
void color(aether_color_rgba actual, aether_color_rgba expected) {
    require(near(actual.r,expected.r) && near(actual.g,expected.g) &&
        near(actual.b,expected.b) && near(actual.a,expected.a), "literal RGBA");
}
struct Style {
    aether_computed_style_t value{};
    explicit Style(aether_computed_style_handle_t h) {
        require(h != nullptr, "non-null style handle");
        require(aether_computed_style_copy(h,&value)==AETHER_OK, "copy style");
    }
    ~Style() { aether_computed_style_free(&value); }
    Style(const Style&)=delete;
    Style& operator=(const Style&)=delete;
};
struct Fixture {
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    std::map<aether_node_t,aether_computed_style_handle_t> handles;
    explicit Fixture(const std::string& css) {
        require(aether_doc_create(&doc)==AETHER_OK,"create document");
        require(aether_doc_parse_html(doc,"<html><body><div id='c'></div></body></html>")==AETHER_OK,"parse document");
        aether_style_environment_t env{200,120,1,16,0,0,0};
        aether_style_runtime_options_t options{};
        require(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK,"create runtime");
        add(css); resolve();
    }
    ~Fixture() {
        for (auto& entry:handles) aether_computed_style_handle_destroy(entry.second);
        aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    }
    aether_node_t node() {
        auto n=aether_doc_get_element_by_id(doc,"c"); require(n!=nullptr,"target node"); return n;
    }
    void add(const std::string& css) {
        int64_t sheet{};
        require(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"repeat-runtime.css",&sheet,nullptr)==AETHER_OK,"add stylesheet");
    }
    void resolve() {
        aether_style_update_t* updates{};
        int count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);
        require(count>=0,"resolve subtree");
        require(count==0 || updates!=nullptr,"update storage");
        for (int i=0;i<count;++i) {
            aether_node_t n{}; aether_computed_style_handle_t h{}; uint32_t impact{};
            require(aether_style_runtime_get_update(updates[i],&n,&h,&impact)==AETHER_OK,"read update");
            require(n!=nullptr && h!=nullptr,"update identities");
            auto previous=handles.find(n);
            if (previous!=handles.end()) aether_computed_style_handle_destroy(previous->second);
            handles[n]=h;
        }
        aether_style_updates_free(updates,count);
    }
    aether_computed_style_handle_t at() {
        auto found=handles.find(node()); require(found!=handles.end(),"target style"); return found->second;
    }
    void set(const std::string& css) {
        require(aether_element_set_attribute(node(),"style",css.c_str())==AETHER_OK,"set inline style"); resolve();
    }
    void clear_cache() { aether_style_runtime_clear_cache(runtime); resolve(); }
};
void layers(const Style& s,int count) {
    require(s.value.background_layers.count==count,"exact image count");
    require(count==0 ? s.value.background_layers.items==nullptr : s.value.background_layers.items!=nullptr,"image storage");
}
void gradient(const aether_background_layer& l,aether_color_rgba first,aether_color_rgba last) {
    require(l.kind==AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT,"linear gradient kind");
    require(l.gradient.stop_count==2 && l.gradient.stops!=nullptr,"two owned stops");
    color(l.gradient.stops[0].color,first); color(l.gradient.stops[1].color,last);
}
void position(const aether_background_layer& l,float x,float y) {
    require(l.position_x.unit==AETHER_CSS_UNIT_PERCENT && l.position_y.unit==AETHER_CSS_UNIT_PERCENT,"percent position units");
    require(near(l.position_x.value,x) && near(l.position_y.value,y),"literal position values");
}
void size(const aether_background_layer& l,float x,float y) {
    require(l.size_kind==AETHER_BACKGROUND_SIZE_EXPLICIT,"explicit size kind");
    require(l.size_x.unit==AETHER_CSS_UNIT_PX && l.size_y.unit==AETHER_CSS_UNIT_PX,"pixel size units");
    require(near(l.size_x.value,x) && near(l.size_y.value,y),"literal size values");
}
void repeat(const aether_background_layer& l,int x,int y) {
    require(l.repeat_x==x && l.repeat_y==y,"literal X/Y repeat enums");
}
const std::string baseline=
    "background-image:linear-gradient(black,white);background-position:17% 23%;"
    "background-size:7px 9px;background-repeat:round space;background-color:#663399;";
const std::string first="linear-gradient(red,blue) left top / 20px 30px repeat no-repeat";
const std::string valid=first+" yellow";
const std::string second="linear-gradient(blue,red) center / 40px 50px space round yellow";
void expect_baseline(const Style& s) {
    layers(s,1); const auto& l=s.value.background_layers.items[0];
    gradient(l,black,white); position(l,17,23); size(l,7,9);
    repeat(l,AETHER_BACKGROUND_ROUND,AETHER_BACKGROUND_SPACE); color(s.value.background_color,purple);
}
void expect_valid(const Style& s,int x=AETHER_BACKGROUND_REPEAT,int y=AETHER_BACKGROUND_NO_REPEAT) {
    layers(s,1); const auto& l=s.value.background_layers.items[0];
    gradient(l,red,blue); position(l,0,0); size(l,20,30); repeat(l,x,y); color(s.value.background_color,yellow);
}
void expect_initial(const Style& s,bool revealed) {
    layers(s,revealed?1:0); color(s.value.background_color,clear);
    if (!revealed) return;
    const auto& l=s.value.background_layers.items[0];
    gradient(l,white,black); position(l,0,0);
    require(l.size_kind==AETHER_BACKGROUND_SIZE_EXPLICIT && l.size_x.unit==AETHER_CSS_UNIT_AUTO &&
        l.size_y.unit==AETHER_CSS_UNIT_AUTO,"native initial size is explicit auto/auto");
    repeat(l,AETHER_BACKGROUND_REPEAT,AETHER_BACKGROUND_REPEAT);
}
} // namespace

int main() {
    { Fixture f("#c{"+baseline+"}"); Style s(f.at()); expect_baseline(s); }
    // Establish the first layer independently before testing a later-layer failure.
    {
        Fixture f("#c{"+baseline+"}"); { Style s(f.at()); expect_baseline(s); }
        f.set("background:"+first); Style s(f.at());
        layers(s,1); const auto& l=s.value.background_layers.items[0];
        gradient(l,red,blue); position(l,0,0); size(l,20,30);
        repeat(l,AETHER_BACKGROUND_REPEAT,AETHER_BACKGROUND_NO_REPEAT); color(s.value.background_color,clear);
    }
    const std::string invalids[]={
        "linear-gradient(red,blue) center / cover repeat no-repeat round yellow",
        "linear-gradient(red,blue) repeat center no-repeat / cover yellow",
        "linear-gradient(red,blue) center / cover repeat-x no-repeat yellow",
        first+",linear-gradient(blue,red) center / cover repeat no-repeat round yellow",
        first+",linear-gradient(blue,red) repeat center no-repeat / cover yellow"
    };
    for (const auto& bad:invalids) {
        // Later stylesheet, important stylesheet, inline, and important inline.
        for (int mode=0;mode<4;++mode) {
            Fixture f("#c{"+baseline+"}"); { Style s(f.at()); expect_baseline(s); }
            std::string declaration="background:"+bad+(mode%2?" !important;":";");
            if (mode<2) { f.add("#c{"+declaration+"}"); f.resolve(); }
            else f.set(declaration);
            Style s(f.at()); expect_baseline(s);
        }
    }
    {
        Fixture f("#c{"+baseline+"}"); { Style s(f.at()); expect_baseline(s); }
        f.set("background:"+first+","+second); Style s(f.at()); layers(s,2);
        const auto& a=s.value.background_layers.items[0]; const auto& b=s.value.background_layers.items[1];
        gradient(a,red,blue); position(a,0,0); size(a,20,30); repeat(a,AETHER_BACKGROUND_REPEAT,AETHER_BACKGROUND_NO_REPEAT);
        gradient(b,blue,red); position(b,50,50); size(b,40,50); repeat(b,AETHER_BACKGROUND_SPACE,AETHER_BACKGROUND_ROUND);
        color(s.value.background_color,yellow);
    }
    const std::string deferred_valid[]={
        "--r:repeat no-repeat;background:linear-gradient(red,blue) left top / 20px 30px var(--r) yellow;",
        "--b:"+valid+";background:var(--b);",
        "background:var(--missing,"+valid+");"};
    for (const auto& declarations:deferred_valid) {
        Fixture f("#c{"+baseline+declarations+"}"); Style s(f.at()); expect_valid(s);
    }
    for (const auto& bad:invalids) {
        Fixture f("#c{"+baseline+"background:var(--b,"+valid+");}");
        f.set("--b:"+bad); { Style s(f.at()); expect_initial(s,false); }
        // Defined but invalid does not select the valid fallback or revive baseline.
        f.set("--b:"+bad+";background-image:linear-gradient(white,black) !important");
        { Style s(f.at()); expect_initial(s,true); }
        f.resolve(); { Style s(f.at()); expect_initial(s,true); }
        f.clear_cache(); { Style s(f.at()); expect_initial(s,true); }
    }
    {
        Fixture f("#c{"+baseline+"background:linear-gradient(red,blue) left top / 20px 30px var(--r,repeat no-repeat) yellow;}");
        f.set("--r:repeat no-repeat"); Style old_copy(f.at()); expect_valid(old_copy);
        aether_computed_style_handle_t old_handle{};
        require(aether_computed_style_handle_clone(f.at(),&old_handle)==AETHER_OK,"retain old valid handle");
        f.resolve(); { Style s(f.at()); expect_valid(s); }
        f.clear_cache(); { Style s(f.at()); expect_valid(s); }
        f.set("--r:repeat no-repeat round"); Style invalid_copy(f.at()); expect_initial(invalid_copy,false);
        aether_computed_style_handle_t invalid_handle{};
        require(aether_computed_style_handle_clone(f.at(),&invalid_handle)==AETHER_OK,"retain old invalid handle");
        f.resolve(); { Style s(f.at()); expect_initial(s,false); }
        f.clear_cache(); { Style s(f.at()); expect_initial(s,false); }
        f.set("--r:no-repeat repeat");
        { Style s(f.at()); expect_valid(s,AETHER_BACKGROUND_NO_REPEAT,AETHER_BACKGROUND_REPEAT); }
        f.resolve(); f.clear_cache();
        { Style s(f.at()); expect_valid(s,AETHER_BACKGROUND_NO_REPEAT,AETHER_BACKGROUND_REPEAT); }
        expect_valid(old_copy); expect_initial(invalid_copy,false);
        { Style s(old_handle); expect_valid(s); }
        { Style s(invalid_handle); expect_initial(s,false); }
        aether_computed_style_handle_destroy(old_handle); aether_computed_style_handle_destroy(invalid_handle);
    }
    const char* words[]={"repeat","no-repeat","space","round"};
    const int enums[]={AETHER_BACKGROUND_REPEAT,AETHER_BACKGROUND_NO_REPEAT,AETHER_BACKGROUND_SPACE,AETHER_BACKGROUND_ROUND};
    for (int x=0;x<4;++x) for (int y=0;y<4;++y) {
        Fixture f("#c{"+baseline+"background-repeat:"+words[x]+" "+words[y]+";}");
        Style s(f.at()); layers(s,1); const auto& l=s.value.background_layers.items[0];
        gradient(l,black,white); position(l,17,23); size(l,7,9); color(s.value.background_color,purple); repeat(l,enums[x],enums[y]);
    }
    for (int alias=0;alias<2;++alias) {
        Fixture f("#c{"+baseline+"background-repeat:"+(alias?"repeat-y":"repeat-x")+";}");
        Style s(f.at()); layers(s,1); repeat(s.value.background_layers.items[0],
            alias?AETHER_BACKGROUND_NO_REPEAT:AETHER_BACKGROUND_REPEAT,
            alias?AETHER_BACKGROUND_REPEAT:AETHER_BACKGROUND_NO_REPEAT);
    }
    {
        Fixture f("#c{"+baseline+"background-image:linear-gradient(red,blue),linear-gradient(blue,red),linear-gradient(white,black);background-repeat:repeat no-repeat,space round;}");
        Style s(f.at()); layers(s,3);
        gradient(s.value.background_layers.items[0],red,blue); gradient(s.value.background_layers.items[1],blue,red);
        gradient(s.value.background_layers.items[2],white,black);
        for (int i=0;i<3;++i) {
            const auto& l=s.value.background_layers.items[i]; position(l,17,23); size(l,7,9);
            repeat(l,i%2?AETHER_BACKGROUND_SPACE:AETHER_BACKGROUND_REPEAT,
                i%2?AETHER_BACKGROUND_ROUND:AETHER_BACKGROUND_NO_REPEAT);
        }
        color(s.value.background_color,purple);
    }
    std::printf("background-repeat runtime: %d checks passed (active with NDEBUG)\n",checks);
    return 0;
}
