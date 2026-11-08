#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace {
int checks = 0;
void require(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL background-position context: %s\n", message); std::exit(1); }
}
bool near(float a, float b) { return std::fabs(a-b) < .001f; }
const char* images = "linear-gradient(red,blue),linear-gradient(green,white),linear-gradient(black,yellow)";
struct Style {
    aether_computed_style_t value{};
    explicit Style(aether_computed_style_handle_t handle) {
        require(aether_computed_style_copy(handle, &value) == AETHER_OK, "copy style");
    }
    ~Style() { aether_computed_style_free(&value); }
    Style(const Style&) = delete;
    Style& operator=(const Style&) = delete;
};
struct Fixture {
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    aether_style_environment_t env{200,120,1,16,0,0,0};
    std::map<aether_node_t,aether_computed_style_handle_t> handles;
    uint32_t child_impact{};
    explicit Fixture(const std::string& css) {
        require(aether_doc_create(&doc)==AETHER_OK, "create document");
        require(aether_doc_parse_html(doc,"<html><body><div id='p'><div id='c'></div></div></body></html>")==AETHER_OK, "parse document");
        aether_style_runtime_options_t options{};
        require(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK,"create runtime");
        int64_t sheet{};
        require(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"position-context.css",&sheet,nullptr)==AETHER_OK,"add sheet");
        resolve();
    }
    ~Fixture() {
        for(auto& item:handles) aether_computed_style_handle_destroy(item.second);
        aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    }
    aether_node_t node(const char* id) { return aether_doc_get_element_by_id(doc,id); }
    aether_computed_style_handle_t at(const char* id) {
        auto found=handles.find(node(id)); require(found!=handles.end(),"find style"); return found->second;
    }
    void consume(aether_style_update_t* updates,int count) {
        require(count>=0,"resolve result"); child_impact=0;
        for(int i=0;i<count;++i) {
            aether_node_t n{};aether_computed_style_handle_t h{};uint32_t impact{};
            require(aether_style_runtime_get_update(updates[i],&n,&h,&impact)==AETHER_OK,"read update");
            if(n==node("c")) child_impact|=impact;
            auto previous=handles.find(n);
            if(previous!=handles.end()) aether_computed_style_handle_destroy(previous->second);
            handles[n]=h;
        }
        aether_style_updates_free(updates,count);
    }
    void resolve() {
        aether_style_update_t* u{};
        int n=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&u);
        consume(u,n);
    }
    void set(const char* id,const std::string& text) {
        require(aether_element_set_attribute(node(id),"style",text.c_str())==AETHER_OK,"set style"); resolve();
    }
    void resize(float width,float height) {
        env.viewport_width=width;env.viewport_height=height;
        aether_style_update_t* u{};
        int n=aether_style_runtime_set_environment(runtime,&env,aether_doc_get_document_element(doc),&u);
        consume(u,n);
    }
};
void position(const aether_background_layer& layer,float x,float y,
              aether_css_unit_t xu=AETHER_CSS_UNIT_PERCENT,aether_css_unit_t yu=AETHER_CSS_UNIT_PERCENT) {
    require(layer.position_x.unit==xu && layer.position_y.unit==yu,"position units");
    require(near(layer.position_x.value,x) && near(layer.position_y.value,y),"position values");
}
void single(Fixture& f,float x,float y,aether_css_unit_t xu=AETHER_CSS_UNIT_PERCENT,aether_css_unit_t yu=AETHER_CSS_UNIT_PERCENT) {
    Style s(f.at("c"));require(s.value.background_layers.count==1,"one image");position(s.value.background_layers.items[0],x,y,xu,yu);
}
void equal_background(const Style& a,const Style& b) {
    require(aether_background_layer_list_equal(&a.value.background_layers,&b.value.background_layers),"all image/position/size/repeat targets unchanged");
    const auto& x=a.value.background_color;const auto& y=b.value.background_color;
    require(x.r==y.r && x.g==y.g && x.b==y.b && x.a==y.a,"color target unchanged");
}
} // namespace

int main() {
    // A parent without images still owns its entire list. Inherited lengths are
    // computed in the parent, and two stored positions repeat modulo three images.
    {
        Fixture f(std::string("html{font-size:16px}#p{font-size:10px;background-image:none;background-position:2em center,center right}#c{font-size:30px;background-position:inherit;background-image:")+images+"}");
        Style parent(f.at("p"));require(parent.value.background_layers.count==0,"positions do not invent parent images");
        Style child(f.at("c"));require(child.value.background_layers.count==3,"three child images");
        position(child.value.background_layers.items[0],20,50,AETHER_CSS_UNIT_PX);
        position(child.value.background_layers.items[1],100,50);
        position(child.value.background_layers.items[2],20,50,AETHER_CSS_UNIT_PX);
        f.set("p","font-size:12px");
        Style changed(f.at("c"));position(changed.value.background_layers.items[0],24,50,AETHER_CSS_UNIT_PX);
        // Previously copied snapshots remain independently owned.
        position(child.value.background_layers.items[0],20,50,AETHER_CSS_UNIT_PX);
    }
    {
        Fixture f("#p{background-position:center left,center right}#c{background-position:inherit}");
        Style empty(f.at("c"));require(empty.value.background_layers.count==0,"unpainted inherited child");
        f.set("c",std::string("background-image:")+images);
        Style s(f.at("c"));require(s.value.background_layers.count==3,"later image list");
        for(int i=0;i<3;++i) position(s.value.background_layers.items[i],i%2?100:0,50);
        require(empty.value.background_layers.count==0,"old unpainted snapshot unchanged");
    }
    {
        Fixture f(std::string("#p{background-image:")+images+";background-position:right bottom}#c{background-position:center left,10px top;background-image:inherit}");
        Style s(f.at("c"));require(s.value.background_layers.count==3,"image-only inheritance count");
        position(s.value.background_layers.items[0],0,50);
        position(s.value.background_layers.items[1],10,0,AETHER_CSS_UNIT_PX);
        position(s.value.background_layers.items[2],0,50);
    }
    for(const auto& input: {std::string("2em"),std::string("1rem"),std::string("10vw"),std::string("10vh")}) {
        Fixture f("html{font-size:16px}#c{font-size:10px;background-image:linear-gradient(red,blue);background-position:"+input+" center}");
        float expected=input=="1rem"?16:input=="10vh"?12:20;
        single(f,expected,50,AETHER_CSS_UNIT_PX);
        if(input=="10vw" || input=="10vh") { f.resize(300,180);single(f,input=="10vw"?30:18,50,AETHER_CSS_UNIT_PX); }
        if(input=="2em") { f.set("c","font-size:15px");single(f,30,50,AETHER_CSS_UNIT_PX); }
    }
    {
        Fixture f("#c{background-image:linear-gradient(red,blue);background-position:calc(25% + 2px) center}");
        Style s(f.at("c"));const auto& x=s.value.background_layers.items[0].position_x;
        require(near(aether_css_length_resolve(x,60,10,16,200,120),17),"calc retains percentage plus pixels");
        require(x.unit==AETHER_CSS_UNIT_LINEAR && near(x.linear_percent,25) && near(x.value,2),"calc coefficients survive storage");
    }
    // Position-only changes may repaint, never request layout. Stable resolves
    // are checked by value without imposing an update-count contract.
    {
        Fixture f("#c{background-image:linear-gradient(red,blue);background-position:20% 30%;background-position:var(--p,90% 90%)}");
        single(f,90,90);
        f.set("c","--p:center left");single(f,0,50);
        require((f.child_impact&AETHER_STYLE_IMPACT_LAYOUT)==0,"position-only change has no layout impact");
        f.resolve();single(f,0,50);
        f.set("c","--p:top 10px");single(f,0,0);
        f.resolve();single(f,0,0);
        f.set("c","--p:left right");single(f,0,0);
        f.set("c","");single(f,90,90);
        aether_style_runtime_clear_cache(f.runtime);f.resolve();single(f,90,90);
        f.set("c","--p:center right");single(f,100,50);
    }
    // Invalid inline input must not mask an eligible stylesheet declaration.
    {
        Fixture f("#c{background-image:linear-gradient(red,blue);background-position:20% 30%}");
        f.set("c","background-position:top 10px !important");single(f,20,30);
    }
    const std::string baseline="background-color:red;background-image:linear-gradient(red,blue);background-position:20% 30%;background-size:31px 17px;background-repeat:space round;";
    for(const char* invalid:{"linear-gradient(white,black) top 10px / 9px 8px no-repeat blue",
                            "linear-gradient(white,black) center left / 9px 8px no-repeat,linear-gradient(yellow,green) top 10px / 7px 6px repeat blue"}) {
        Fixture f("#c{"+baseline+"}");Style before(f.at("c"));
        f.set("c","background:"+std::string(invalid));Style after(f.at("c"));equal_background(before,after);
    }
    {
        Fixture f("#c{"+baseline+"background:var(--b)}");
        f.set("c","--b:linear-gradient(white,black) center left / 20px 10px no-repeat blue");
        Style valid(f.at("c"));require(valid.value.background_layers.count==1,"deferred valid image");
        const auto& layer=valid.value.background_layers.items[0];position(layer,0,50);
        require(layer.size_kind==AETHER_BACKGROUND_SIZE_EXPLICIT && near(layer.size_x.value,20) && near(layer.size_y.value,10),"deferred valid size");
        require(layer.repeat_x==AETHER_BACKGROUND_NO_REPEAT && layer.repeat_y==AETHER_BACKGROUND_NO_REPEAT,"deferred valid repeat");
        require(valid.value.background_color.b>0 && valid.value.background_color.r==0,"deferred valid color");
        for(const char* invalid:{"linear-gradient(white,black) top 10px / 9px 8px repeat green",
                               "linear-gradient(white,black) center left,linear-gradient(red,blue) top 10px green"}) {
            f.set("c","--b:"+std::string(invalid));Style rejected(f.at("c"));
            require(rejected.value.background_layers.count==0,"invalid deferred shorthand uses initial image, not earlier layer");
            require(rejected.value.background_color.a==0,"invalid deferred shorthand uses initial transparent color");
        }
        f.set("c","--b:linear-gradient(white,black) center right / 20px 10px no-repeat blue");single(f,100,50);
    }
    std::printf("background-position contextual checks: %d passed (active with NDEBUG)\n",checks);
}
