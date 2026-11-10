// Private direct-measurement budget: no layout pass or timing noise.
#include "../src/layout/layout_internal.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cmath>
static void require(bool ok,const char* what) {
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",what); std::exit(1); }
}
static aether_text_metrics_t measure_empty(const char*, const aether_computed_style_t*, float, void*) {
    return {0.f, 0.f, 0.f};
}

struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t styles = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_style_update_t* updates = nullptr;
    int32_t update_count = 0;

    Fixture(const char* html, const char* css, aether_text_measurer_fn measurer = measure_empty) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc, html) == AETHER_OK, "parse HTML");
        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{640, 480, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
                "create style runtime");
        int64_t sheet = 0;
        int32_t rules = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css, 0, "flex-inflexible-intrinsic.css", &sheet, &rules)
                    == AETHER_OK, "add stylesheet");
        update_count = aether_style_runtime_resolve_subtree(
            runtime, aether_doc_get_document_element(doc), &updates);
        require(update_count > 0, "resolve styles through runtime");
        require(aether_style_runtime_create_resolved_style_map(runtime, doc, &styles) == AETHER_OK,
                "create resolved style map");
        require(aether_layout_create(measurer, nullptr, &engine) == AETHER_OK, "create layout engine");
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    ~Fixture() {
        aether_layout_destroy(engine);
        aether_style_map_destroy(styles);
        aether_style_updates_free(updates, update_count);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(doc);
    }

    aether_node_t node(const char* id) const {
        const auto element = aether_doc_get_element_by_id(doc, id);
        require(element != nullptr, "find fixture element");
        return element;
    }

    aether_layout_result_t layout(float width, float height) const {
        aether_layout_result_t result = nullptr;
        require(aether_layout_layout(engine, doc, styles, width, height, nullptr, nullptr, &result)
                    == AETHER_OK && result != nullptr, "perform layout");
        return result;
    }

    aether_rectf rect(aether_layout_result_t result, const char* id) const {
        const auto box = aether_layout_result_find_primary_box(result, node(id));
        require(box != nullptr, "find primary layout box");
        return aether_layout_box_get_border_box(box);
    }
};


static long measures=0;
static aether_text_metrics_t counted(const char* text,const aether_computed_style_t*,float,void*) {
    ++measures; return {10.f*float(std::strlen(text)),20.f,15.f};
}
int main() {
    int cases=0;
    for(const char* basis : {"content","20px","50%","calc(0% + 20px)","auto"}) {
        for(int leaves : {1,3}) {
            long baseline[2]={0,0};
            for(int depth : {1,2,4,8,16,32,64,128}) {
                std::string html="<body><div id='inner' class='w'>";
                for(int i=1;i<depth;++i) html+="<div class='w'>";
                // Distinct leaves prevent a one-node-only cache from satisfying
                // this budget; whitespace has no anonymous box of its own.
                for(int i=0;i<leaves;++i) html+="<span>aaaa bb cc</span>";
                for(int i=0;i<depth;++i) html+="</div>";
                html+="</body>";
                const std::string css="*{margin:0;padding:0;border:0;}"
                    "body{display:flex;width:800px;}span{display:block;min-width:0;}"
                    ".w{display:flex;flex-wrap:wrap;min-width:0;flex:0 1 "+std::string(basis)+";}";
                Fixture f(html.c_str(),css.c_str(),counted);
                aether::style::FrozenStyleRef defaults;
                require(aether::style::freeze_style(aether_computed_style_default(),defaults)==AETHER_OK,
                        "freeze defaults");
                auto* result=new aether_layout_result_s();
                result->build_overlay=std::make_unique<aether::layout::MutableLayoutOverlay>();
                const auto version=aether::style::pin_style_map(f.styles);
                const aether::layout::StyleLookup lookup(version,result->build_overlay.get(),defaults);
                f.engine->measure_styles=&lookup;
                f.engine->measure_document=f.doc->identity;
                f.engine->viewport_width=900; f.engine->viewport_height=700;
                int index=0;
                for(auto mode : {aether_intrinsic_size_mode_e::min_content,
                                 aether_intrinsic_size_mode_e::max_content}) {
                    // Repeat on the same engine and overlay: caches must not
                    // change answers or grow the work required by a new call.
                    for(int pass=0;pass<2;++pass) {
                        measures=0;
                        const float width=measure_intrinsic_width(f.engine,&lookup,f.node("inner"),
                            get_style(&lookup,f.node("inner")),{},{},900,result,mode);
                        const float expected=index==0?40.f:
                            (depth>1 && std::strcmp(basis,"20px")==0?40.f:100.f*leaves);
                        std::printf("basis=%s leaves=%d depth=%d mode=%d pass=%d calls=%ld width=%g\n",
                                    basis,leaves,depth,index,pass,measures,width);
                        require(std::isfinite(width) && std::fabs(width-expected)<.001f,
                                "natural minimum and selected maximum");
                        // Baseline before this change was five callbacks per
                        // leaf per mode. Paired min/max must preserve that budget,
                        // independent of depth, never a recursively doubled walk.
                        require(measures<=5*leaves,"constant callback budget per text leaf");
                        if(depth==1 && pass==0) baseline[index]=measures;
                        require(measures<=baseline[index],"depth-independent measurement budget");
                        ++cases;
                    }
                    ++index;
                }
                f.engine->measure_styles=nullptr;
                f.engine->measure_document.reset();
                aether_layout_result_destroy(result);
            }
        }
    }
    std::printf("wrap intrinsic measurement: %d cases passed\n",cases);
}
