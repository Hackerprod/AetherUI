#pragma once
// Public ABI only: also safe to run as a standalone consumer against an older DLL.
#include <aether/aether.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
namespace content_hit_contract {
inline int failures=0, assertions=0;
inline std::string context;
inline void check(bool good,const char* message) {
    ++assertions; if(!good){++failures;std::fprintf(stderr,"FAIL content-hit [%s] %s\n",context.c_str(),message);}
}
inline void ok(aether_status_t status){if(status!=AETHER_OK){std::fprintf(stderr,"ABI failure %d\n",status);std::abort();}}
inline aether_text_metrics_t measure(const char* text,const aether_computed_style_t*,float,void*){
    return {text?float(std::strlen(text))*8.f:0.f,16.f,12.f};
}
struct Fixture {
    aether_doc_t doc=nullptr; aether_style_runtime_t runtime=nullptr;
    aether_style_map_t styles=nullptr; aether_layout_engine_t engine=nullptr; aether_layout_result_t layout=nullptr;
    Fixture(const std::string& css,const char* html="<body><div id='container'><div id='a'></div><div id='b'></div></div></body>"){
        ok(aether_doc_create(&doc));ok(aether_doc_parse_html(doc,html));
        aether_style_runtime_options_t options{};aether_style_environment_t env{360,340,1,16,0,0,0};
        ok(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime));int64_t sheet=0;
        const auto full="html,body{display:block;margin:0}div{display:block}#container{position:absolute;left:70px;top:70px;width:180px;height:200px}#a{width:50px;height:50px}#b{width:140px;height:50px}"+css;
        ok(aether_style_runtime_add_stylesheet(runtime,full.c_str(),2,"content-hit.css",&sheet,nullptr));
        aether_style_update_t* updates=nullptr;auto count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);
        check(count>0,"styles resolve");aether_style_updates_free(updates,count);
        ok(aether_style_runtime_create_resolved_style_map(runtime,doc,&styles));ok(aether_layout_create(measure,nullptr,&engine));
        ok(aether_layout_layout(engine,doc,styles,360,340,nullptr,nullptr,&layout));
    }
    ~Fixture(){aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(styles);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
    aether_node_t node(const char* id){return id?aether_doc_get_element_by_id(doc,id):nullptr;}
    aether_rectf rect(const char* id){return aether_layout_box_get_border_box(aether_layout_result_find_primary_box(layout,node(id)));}
};
inline aether_scroll_offset_t scroll(aether_node_t node,void* data){return node==data?aether_scroll_offset_t{10,10}:aether_scroll_offset_t{};}
inline void hit(Fixture& f,float x,float y,const char* id,bool scrolled=false,const char* plain_id=nullptr){
    auto expected=f.node(id);auto cb=scrolled?scroll:nullptr;void* data=scrolled?f.node("container"):nullptr;
    check(aether_layout_result_hit_test(f.layout,x,y)==f.node(plain_id?plain_id:id),"DOM plain owner");
    check(aether_layout_result_hit_test_with_scroll_offsets(f.layout,x,y,cb,data)==expected,"DOM snapshot owner");
    check(aether_layout_result_hit_test_with_styles_and_scroll_offsets(f.layout,f.styles,x,y,cb,data)==expected,"DOM current owner");
    for(auto map:{aether_style_map_t(nullptr),f.styles}){
        aether_input_hit_part_t part=-1;aether_node_t owner=nullptr;
        ok(aether_layout_result_hit_test_input(f.layout,map,x,y,cb,data,&part,&owner));
        check(part==(expected?AETHER_INPUT_HIT_CONTENT:AETHER_INPUT_HIT_NONE),"input snapshot/current part");
        check(owner==expected,"input snapshot/current owner");
    }
}
inline int run(){
    failures=assertions=0;
    struct Probe{const char* name;const char* css;float x,y;const char* owner;};
    const Probe probes[]={
        {"zero","",195,125,"b"},
        {"negative top exclusive","#b{margin-top:-25px}",195,100,"b"},
        {"positive top white","#b{margin-top:25px}",140,135,"container"},
        {"negative bottom","#b{margin-bottom:-25px}",75,165,"b"},
        {"positive bottom white","#b{margin-bottom:25px}",140,180,"container"},
        {"negative left outside ancestor","#b{margin-left:-25px}",50,145,"b"},
        {"positive left white","#b{margin-left:25px}",85,145,"container"},
        {"negative right","#b{margin-right:-25px}",205,145,"b"},
        {"positive right white","#b{margin-right:25px}",220,145,"container"},
        {"beyond top","#b{margin-top:-75px}",195,50,"b"},
        {"beyond top inclusive bottom","#b{margin-top:-75px}",95,95,"b"},
        {"beyond bottom","#b{margin-bottom:-75px}",195,125,"b"},
        {"beyond left","#b{margin-left:-175px}",30,145,"b"},
        {"beyond left logical query outside viewport","#b{margin-left:-175px}",-100,125,"b"},
        {"beyond right","#b{margin-right:-175px}",195,125,"b"},
        {"mixed visible","#b{margin:-25px 25px 25px -25px}",170,100,"b"},
        {"mixed white","#b{margin:-25px 25px 25px -25px}",195,120,"container"},
        {"relative","#b{margin-top:-25px;position:relative;left:20px;top:15px}",215,115,"b"},
        {"z over","#a{position:relative;z-index:1}#b{margin-top:-25px;position:relative;z-index:2}",75,100,"b"},
        {"z under inclusive A edge","#a{position:relative;z-index:1}#b{margin-top:-25px;position:relative;z-index:-1}",75,120,"a"},
        {"negative clipped","#container{overflow:hidden}#b{margin-left:-25px}",50,145,nullptr},
        {"positive clipped white","#container{overflow:hidden}#b{margin-left:25px}",85,145,"container"},
        {"exterior","",300,300,nullptr},
    };
    for(auto& p:probes){context=p.name;Fixture f(p.css);hit(f,p.x,p.y,p.owner);}
    context="scroll callback contracts";{Fixture f("#container{overflow:auto;width:100px;height:60px}#b{margin-top:-25px}");hit(f,95,90,"b",true,"a");hit(f,130,110,"b",true,"b");hit(f,85,85,"b",true,"a");
        // Snapshot offsets are zero here; without a callback input retains that snapshot.
        hit(f,95,90,"a");hit(f,130,110,"b");hit(f,85,85,"a");}
    // Transparent boxes still contribute their whole border, including outside a padding clip.
    context="border padding content and margin";{Fixture f("#b{margin:20px;padding:10px;border:5px solid transparent;overflow:hidden}");
        auto r=f.rect("b");hit(f,r.x+2,r.y+2,"b");hit(f,r.x+8,r.y+8,"b");hit(f,r.x+20,r.y+20,"b");hit(f,r.x-5,r.y+20,"container");
        hit(f,r.x,r.y,"b");hit(f,r.x+r.width,r.y+r.height,"b");}
    // Exact zero-size lines/points remain eligible; epsilon outside must miss self.
    for(auto css:{"#b{width:0;height:30px}","#b{width:30px;height:0}","#b{width:0;height:0}"}){
        context=css;Fixture f(css);auto r=f.rect("b");hit(f,r.x,r.y,"b");hit(f,r.x+.1f,r.y+.1f,r.width==0||r.height==0?"container":"b");}
    // Negative rect dimensions retain the public primitive's existing inequality semantics.
    context="public rect degenerates";
    check(aether_rectf_contains({10,20,0,0},{10,20}),"zero point contains exact point");
    check(!aether_rectf_contains({10,20,-1,5},{10,22}),"negative width has no finite interior");
    check(!aether_rectf_contains({10,20,5,-1},{12,20}),"negative height has no finite interior");
    context="atomic and text ownership";{Fixture f("#b{display:inline-block;margin-left:-25px}","<body><div id='container'><div id='a'></div><button id='b'>hi</button></div></body>");auto r=f.rect("b");hit(f,r.x+2,r.y+2,"b");}
    {Fixture f("#b{margin-left:-25px}","<body><div id='container'><div id='a'></div><div id='b'>hi</div></div></body>");auto r=f.rect("b");hit(f,r.x+2,r.y+2,"b");}
    std::printf("content-hit: %d assertions, %d failures\n",assertions,failures);return failures;
}
}
