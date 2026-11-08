#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <aether/aether_contextual_layout.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

// All values are fictitious. No assert, private headers, production decoder or
// generated mask oracle is used. Case/check IDs are the compact evidence API.
namespace {
constexpr const char* B1 = "\xe2\x80\xa2";
constexpr const char* B2 = "\xe2\x80\xa2\xe2\x80\xa2";
constexpr const char* B3 = "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2";
constexpr const char* B4 = "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2";
constexpr const char* B5 = "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2";
constexpr const char* B12 = "••••••••••••";
static_assert(sizeof("••••••••••••") == 37, "literal twelve-scalar oracle is 36 bytes");
static unsigned cases = 0, failed_cases = 0, blocked_cases = 0, details = 0;
static unsigned long long checks = 0, failures = 0;
struct Blocked {};
struct Case {
    const char* id;
    uint64_t seen = 0, bad = 0, required = 0;
    unsigned long long count = 0, failed = 0;
    bool blocked = false;
    explicit Case(const char* name):id(name) {}
    void expect(unsigned n) { required |= uint64_t(1) << n; }
    void check(unsigned n, bool yes, const char* actual=nullptr) {
        expect(n); seen |= uint64_t(1) << n; ++count; ++checks;
        if (!yes) {
            bad |= uint64_t(1) << n; ++failed; ++failures;
            if (details < 8) {
                if(actual) {
                    uint64_t hash=1469598103934665603ULL;
                    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(actual);*p;++p){hash^=*p;hash*=1099511628211ULL;}
                    std::printf("DETAIL %s check=%u len=%zu hash=%016llx\n",id,n,std::strlen(actual),static_cast<unsigned long long>(hash));
                } else std::printf("DETAIL %s check=%u mismatch\n",id,n);
                ++details;
            }
        }
    }
    void api(aether_status_t s) { check(0,s == AETHER_OK); if(s != AETHER_OK) throw Blocked{}; }
    void need(bool b) { check(0,b); if(!b) throw Blocked{}; }
    void finish() {
        if(required & ~seen)blocked=true;
        ++cases; if (failed || blocked) ++failed_cases; if(blocked) ++blocked_cases;
        // A blocked case conservatively marks its entire logical obligation set
        // pending, including later repetitions of an already evaluated check.
        const auto pending = blocked ? required : required & ~seen;
        std::printf("%s %s n=%llu f=%llu bad=%016llx pending=%016llx\n",id,
            blocked?"BLOCKED":(failed?"FAIL":"PASS"),count,failed,
            static_cast<unsigned long long>(bad),static_cast<unsigned long long>(pending));
    }
};
static bool near(float a,float b) {return std::isfinite(a) && std::fabs(a-b)<0.01f;}
static bool raw_absent(const char* actual,const char* raw,const char* expected) {
    if(!actual)return false;
    if(!raw || !*raw)return true;
    // Invalid single bytes 80 and E2 also occur inside the required UTF-8
    // bullet. In that collision, require the entire independent literal;
    // rejecting that byte would incorrectly reject the mask itself.
    if(std::strstr(expected,raw))return std::strcmp(actual,expected)==0;
    return std::strstr(actual,raw)==nullptr;
}
struct Fixture;
struct Measure {
    Case* c{};
    Fixture* owner{};
    const char* expected = B12;
    const char* secret = "DemoSecret42";
    bool hidden = true, neighbors = false, armed = false;
    int action = 0; // 1=value, 2=type, 3=release map, 4=release layout
    unsigned calls = 0, fired = 0;
    aether_style_map_t drop_map{};
    aether_layout_result_t drop_layout{};
};
static aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*);
static aether_text_metrics_t contextual_measure(const char* text,const aether_computed_style_t* style,
    float line_height,float width,void* data) {
    auto& m=*static_cast<Measure*>(data);m.c->check(14,near(line_height,30.f));
    return measure(text,style,width,data);
}
struct Fixture {
    Case& c; bool contextual;
    aether_doc_t doc{}; aether_node_t node{};
    aether_style_runtime_t rt{}; aether_layout_engine_t engine{};
    aether_display_list_builder_t builder{};
    aether_style_map_t maps[8]{}; aether_style_revision_t revisions[8]{};
    aether_layout_result_t layouts[8]{};
    unsigned nm=0,nl=0;
    aether_style_map_t map{};
    aether_style_environment_t env{400,240,1,16,0,0,0};
    Measure m;
    explicit Fixture(Case& cc,bool ctx):c(cc),contextual(ctx) {m.c=&c;m.owner=this;}
    ~Fixture() {
        if(builder)aether_display_list_builder_destroy(builder);
        for(auto l:layouts)if(l)aether_layout_result_destroy(l);
        if(engine)aether_layout_destroy(engine);
        for(auto x:maps)if(x)aether_style_map_destroy(x);
        for(auto x:revisions)if(x)aether_style_revision_destroy(x);
        if(rt)aether_style_runtime_destroy(rt);
        if(doc)aether_doc_destroy(doc);
    }
    void init(const char* type,const char* value,const char* placeholder,const std::string& css,
              bool context_callback=false,bool neighbors=false) {
        c.api(aether_doc_create(&doc));
        const char* plain="<html><body><div id='container'><input id='probe'></div></body></html>";
        const char* with_neighbors="<html><body><div id='container'><input id='probe'><span>Sibling</span><button>Button</button></div></body></html>";
        c.api(aether_doc_parse_html(doc,neighbors?with_neighbors:plain));
        node=aether_doc_get_element_by_id(doc,"probe");c.need(node!=nullptr);
        if(type)c.api(aether_element_set_attribute(node,"type",type));
        if(value)c.api(aether_element_set_attribute(node,"value",value));
        if(placeholder)c.api(aether_element_set_attribute(node,"placeholder",placeholder));
        c.api(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&rt));
        int64_t sheet=0;
        const std::string rules="*{margin:0;padding:0;border:0;box-sizing:content-box;}"
            "html,body,div{display:block;}input{display:inline-block;width:140px;height:auto;"
            "font-size:12px;line-height:18px;color:rgb(17,34,51);}button{display:inline-block;}"+css+
            (context_callback ? "input{line-height:30px;}" : "");
        c.api(aether_style_runtime_add_stylesheet(rt,rules.c_str(),0,"password-fixture",&sheet,nullptr));
        m.neighbors=neighbors;
        refresh();
        c.api(aether_layout_create(measure,&m,&engine));
        c.api(aether_display_list_builder_create(measure,&m,&builder));
        if(context_callback) {
            aether_layout_set_contextual_text_measurer(engine,contextual_measure,&m);
            aether_display_list_builder_set_contextual_text_measurer(builder,contextual_measure,&m);
        }
    }
    void refresh() {
        c.need(nm<8);
        const auto i=nm++;
        if(contextual) {
            aether_style_transaction_t tx{};
            c.api(aether_style_runtime_prepare_revision(rt,aether_doc_get_document_element(doc),&env,&tx));
            const auto status=aether_style_runtime_commit_revision(rt,tx,&revisions[i]);
            aether_style_transaction_destroy(tx);c.api(status);
            c.api(aether_style_revision_create_map(revisions[i],&maps[i]));
        } else {
            aether_style_update_t* updates{};
            int n=aether_style_runtime_resolve_subtree(rt,aether_doc_get_document_element(doc),&updates);
            if(n>=0)aether_style_updates_free(updates,n);c.need(n>=0);
            c.api(aether_style_runtime_create_resolved_style_map(rt,doc,&maps[i]));
        }
        map=maps[i];
    }
    void release_map(aether_style_map_t x) {
        for(auto& p:maps)if(p==x){aether_style_map_destroy(p);p=nullptr;if(map==x)map=nullptr;return;}
        c.check(15,false);
    }
    void release_layout(aether_layout_result_t x) {
        for(auto& p:layouts)if(p==x){aether_layout_result_destroy(p);p=nullptr;return;}
        c.check(15,false);
    }
    void dom(const char* expected) {
        char* actual=aether_element_get_attribute(node,"value");
        c.check(1,expected ? actual && std::strcmp(actual,expected)==0 : actual==nullptr);
        aether_string_free(actual);
    }
    aether_layout_result_t layout() {
        c.need(nl<8);auto& result=layouts[nl++];
        c.api(aether_layout_layout(engine,doc,map,400,240,nullptr,nullptr,&result));c.need(result!=nullptr);
        return result;
    }
    aether_layout_result_t clone(aether_layout_result_t source) {
        c.need(nl<8);auto& result=layouts[nl++];c.api(aether_layout_clone_snapshot(source,&result));c.need(result!=nullptr);return result;
    }
};
static aether_text_metrics_t measure(const char* text,const aether_computed_style_t*,float,void* data) {
    auto& m=*static_cast<Measure*>(data);++m.calls;
    const bool neighbor=m.neighbors && text && (std::strcmp(text,"Sibling")==0 || std::strcmp(text,"Button")==0);
    m.c->check(3,text && (std::strcmp(text,m.expected)==0 || neighbor),text);
    if(m.hidden && m.secret && *m.secret)m.c->check(13,raw_absent(text,m.secret,m.expected),text);
    if(m.armed && !m.fired) {
        ++m.fired;
        if(m.action==1)m.c->check(15,aether_element_set_attribute(m.owner->node,"value","Changed7")==AETHER_OK);
        if(m.action==2)m.c->check(15,aether_element_set_attribute(m.owner->node,"type","text")==AETHER_OK);
        if(m.action==3){m.owner->release_map(m.drop_map);m.c->check(15,true);}
        if(m.action==4){m.owner->release_layout(m.drop_layout);m.c->check(15,true);}
    }
    // Table oracle: no counting units/bytes to derive metrics. Deliberately
    // different raw metric catches caret/height leaks independently of payload.
    if(text && m.secret && *m.secret && std::strcmp(text,m.secret)==0)return {113,29,14};
    return {37,19,14};
}
struct List {
    aether_display_list_t value{};
    ~List(){if(value)aether_display_list_destroy(value);}
};
static void scan(Fixture& f,aether_display_list_t list,const char* expected,bool focus,int shadows,
                 aether_rectf content,float font,bool neighbors=false) {
    unsigned texts=0, shadow_count=0, carets=0, siblings=0,buttons=0;
    for(int i=0;i<aether_display_list_get_count(list);++i) {
        aether_paint_command_t cmd{};f.c.api(aether_display_list_get_command(list,i,&cmd));
        const char* payload=nullptr;
        if(cmd.tag==AETHER_PAINT_DRAW_TEXT) {
            payload=cmd.data.draw_text.text;
            if(neighbors && payload && std::strcmp(payload,"Sibling")==0)++siblings;
            else if(neighbors && payload && std::strcmp(payload,"Button")==0)++buttons;
            else {++texts;f.c.check(5,payload && std::strcmp(payload,expected)==0,payload);f.c.check(12,near(cmd.data.draw_text.font_size,font));}
        }
        if(cmd.tag==AETHER_PAINT_DRAW_TEXT_SHADOW) {
            ++shadow_count;payload=cmd.data.draw_text_shadow.text;f.c.check(7,payload && std::strcmp(payload,expected)==0,payload);
        }
        if(cmd.tag==AETHER_PAINT_DRAW_TEXT_GRADIENT) {payload=cmd.data.draw_text_gradient.text;f.c.check(5,false);}
        if(payload && f.m.hidden && f.m.secret && *f.m.secret)f.c.check(13,raw_absent(payload,f.m.secret,expected),payload);
        if(cmd.tag==AETHER_PAINT_FILL_ROUNDED_RECT) {
            const auto& p=cmd.data.fill_rounded_rect;
            if(near(p.rect.width,1.f) && near(p.color.r,17.f/255) && near(p.color.g,34.f/255) && near(p.color.b,51.f/255)) {
                ++carets;
                const float metric=(!f.m.hidden && f.m.secret && std::strcmp(expected,f.m.secret)==0)?113.f:37.f;
                f.c.check(9,near(p.rect.x,std::min(content.x+content.width-1,content.x+metric+1)));
                f.c.check(10,near(p.rect.y,content.y+std::max(1.f,(content.height-font*1.15f)/2)) &&
                    near(p.rect.height,std::max(1.f,std::min(content.height-2,font*1.15f))));
            }
        }
        aether_paint_command_free(&cmd);
    }
    f.c.check(4,texts==1);f.c.check(6,shadow_count==static_cast<unsigned>(shadows));
    f.c.check(8,carets==static_cast<unsigned>(focus));
    if(neighbors)f.c.check(20,siblings==1 && buttons==1);
}
static void paint(Fixture& f,aether_layout_result_t layout,const char* expected,bool focus=true,int shadows=0,
                  float font=12,bool node_only=false,bool neighbors=false,List* retain=nullptr) {
    const auto box=aether_layout_result_find_primary_box(layout,f.node);f.c.need(box!=nullptr);
    const auto content=aether_layout_box_get_content_box(box);
    f.c.check(11,std::isfinite(content.width) && content.width>0 && std::isfinite(content.height) && content.height>0);
    List local;auto& out=retain?*retain:local;
    f.m.expected=expected;
    if(node_only)f.c.api(aether_display_list_build_node_with_styles(f.builder,layout,nullptr,f.node,focus?f.node:nullptr,&out.value));
    else f.c.api(aether_display_list_build(f.builder,layout,focus?f.node:nullptr,&out.value));
    f.c.need(out.value!=nullptr);scan(f,out.value,expected,focus,shadows,content,font,neighbors);
}
static void obligations(Case& c,bool focus,bool hidden,bool shadow,bool context,bool neighbors=false) {
    for(unsigned id:{0u,1u,2u,3u,4u,5u,6u,8u,11u,12u})c.expect(id);
    if(focus){c.expect(9);c.expect(10);}if(hidden)c.expect(13);if(shadow)c.expect(7);
    if(context)c.expect(14);if(neighbors)c.expect(20);
}
static void basic(Case& c,bool ctx,const char* input,const char* expected,const char* type="password",
    const char* placeholder=nullptr,const std::string& css="",bool hidden=true,bool focus=true,int shadows=0,
    float font=12,bool context_callback=false,bool neighbors=false,float height=-1,float width=-1) {
    obligations(c,focus,hidden && input && *input,shadows!=0,context_callback,neighbors);
    if(height>=0)c.expect(21);if(width>=0)c.expect(22);
    Fixture f(c,ctx);f.m.expected=expected;f.m.secret=input;f.m.hidden=hidden;
    f.init(type,input,placeholder,css,context_callback,neighbors);f.dom(input);
    auto layout=f.layout();
    if(height>=0 || width>=0) {
        auto box=aether_layout_result_find_primary_box(layout,f.node);c.need(box!=nullptr);
        auto rect=aether_layout_box_get_content_box(box);
        if(height>=0)c.check(21,near(rect.height,height));
        if(width>=0)c.check(22,near(rect.width,width));
    }
    paint(f,layout,expected,focus,shadows,font,false,neighbors);f.dom(input);
    c.check(2,f.m.calls>0);
}
struct Row {const char* name;const char* input;const char* output;};
static const Row unicode_rows[]={
    {"ascii","DemoSecret42",B12},{"bmp","\xc3\xa9",B1},{"bmp2","\xe6\x9d\xb1\xe4\xba\xac",B2},
    {"supp","\xf0\x9f\x98\x80",B1},{"comb","e\xcc\x81",B2},{"mark","\xcc\x81",B1},
    {"zwj","\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb",B3},
    {"flag","\xf0\x9f\x87\xaa\xf0\x9f\x87\xb8",B2},
    {"modifier","\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd",B2},
    {"hangul","\xe1\x84\x80\xe1\x85\xa1\xe1\x86\xa8",B3},{"max","\xf4\x8f\xbf\xbf",B1},
    {"overlong","\xc0\xaf",B2},{"surrogate","\xed\xa0\x80",B3},{"range","\xf4\x90\x80\x80",B4},
    {"trunc3","\xe2\x82",B2},{"cont","\x80",B1},{"mixed","\x41\xe2\x28\xa1\x42",B5},
    {"min2","\xc2\x80",B1},{"max2","\xdf\xbf",B1},{"min3","\xe0\xa0\x80",B1},
    {"overlong3","\xe0\x9f\xbf",B3},{"presurrogate","\xed\x9f\xbf",B1},
    {"min4","\xf0\x90\x80\x80",B1},{"overlong4","\xf0\x8f\xbf\xbf",B4},
    {"cut2","\xc2",B1},{"cut3","\xe2",B1},{"cut4","\xf0",B1},
    {"cut4b","\xf0\x90",B2},{"cut4c","\xf0\x90\x80",B3},{"f5","\xf5",B1},{"ff","\xff",B1},
    {"bad2","\xc2\x41",B2},{"bad3b","\xe2\x41\x80",B3},{"bad3c","\xe2\x82\x41",B3},
    {"bad4b","\xf1\x41\x80\x80",B4},{"bad4c","\xf1\x80\x41\x80",B4},{"bad4d","\xf1\x80\x80\x41",B4}
};
static_assert(sizeof(unicode_rows)/sizeof(Row)==37,"frozen Unicode inventory");
template<class Fn> void run(const char* group,const char* name,bool ctx,Fn fn) {
    char id[25];const auto n=std::snprintf(id,sizeof(id),"%s-%s-%c",group,name,ctx?'C':'L');
    if(n<0 || n>=static_cast<int>(sizeof(id))) {std::fprintf(stderr,"INFRA case ID overflow\n");std::exit(2);}
    Case c(id);
    try {fn(c);}catch(const Blocked&){c.blocked=true;}catch(const std::exception&){c.check(0,false);c.blocked=true;}
    c.finish();
}
static void sequence(Case& c,bool ctx) {
    obligations(c,true,true,false,false);if(ctx){c.expect(16);c.expect(17);}c.expect(18);c.expect(19);
    Fixture f(c,ctx);f.m.expected=B2;f.m.secret="Aa";f.init("password","Aa","Type here","");f.dom("Aa");
    auto l0=f.layout();auto clone=f.clone(l0);List retained;paint(f,l0,B2,true,0,12,false,false,&retained);
    paint(f,clone,B2);const auto old_map=f.map;
    c.api(aether_element_set_attribute(f.node,"value","BbC"));f.dom("BbC");f.m.secret="BbC";
    if(ctx) {
        auto sentinel=l0;
        const auto s=aether_layout_layout(f.engine,f.doc,old_map,400,240,nullptr,nullptr,&sentinel);
        c.check(16,s==AETHER_ERROR_INVALID_STATE);c.check(17,sentinel==l0);
        if(s==AETHER_OK && sentinel!=l0)aether_layout_result_destroy(sentinel);
        paint(f,l0,B2);paint(f,clone,B2);
    } else {paint(f,l0,B3);paint(f,clone,B3);}
    f.m.expected=B3;f.refresh();auto l1=f.layout();paint(f,l1,B3);paint(f,l1,B3,true,0,12,true);
    c.api(aether_element_set_attribute(f.node,"type","text"));f.m.hidden=false;f.m.expected="BbC";f.refresh();auto l2=f.layout();paint(f,l2,"BbC");
    if(ctx){f.m.hidden=true;paint(f,l0,B2);paint(f,clone,B2);}else{paint(f,l0,"BbC");paint(f,clone,"BbC");}
    c.api(aether_element_set_attribute(f.node,"type","password"));f.m.hidden=true;f.m.expected=B3;f.refresh();auto l3=f.layout();paint(f,l3,B3);
    if(ctx){f.m.hidden=false;paint(f,l2,"BbC");f.m.hidden=true;paint(f,l0,B2);paint(f,clone,B2);}else paint(f,l2,B3);
    c.api(aether_element_set_attribute(f.node,"value",""));f.m.secret="";f.m.expected="Type here";f.refresh();auto l4=f.layout();paint(f,l4,"Type here");f.dom("");
    auto box=aether_layout_result_find_primary_box(l0,f.node);c.need(box!=nullptr);
    scan(f,retained.value,B2,true,0,aether_layout_box_get_content_box(box),12);c.check(18,true);
    c.check(19,f.m.calls>0);c.check(2,f.m.calls>0);
}
static void callback_case(Case& c,bool ctx,bool caret,int action) {
    obligations(c,true,true,false,false);for(unsigned id:{15u,16u,17u,19u})c.expect(id);
    Fixture f(c,ctx);f.init("password","DemoSecret42",nullptr,"");f.dom("DemoSecret42");
    auto prior=f.layout();List old;paint(f,prior,B12,true,0,12,false,false,&old);
    const auto content=aether_layout_box_get_content_box(aether_layout_result_find_primary_box(prior,f.node));
    f.m.calls=0;f.m.action=action;f.m.drop_map=f.map;f.m.drop_layout=prior;f.m.armed=true;
    if(caret) {
        auto sentinel=old.value;
        const auto s=aether_display_list_build(f.builder,prior,f.node,&sentinel);
        f.m.armed=false;
        c.check(16,s==(action<=2?AETHER_ERROR_INVALID_STATE:AETHER_OK));
        c.check(17,action<=2?sentinel==old.value:sentinel && sentinel!=old.value);
        if(s==AETHER_OK && sentinel!=old.value){scan(f,sentinel,B12,true,0,content,12);aether_display_list_destroy(sentinel);}
    } else {
        auto sentinel=prior;
        const auto s=aether_layout_layout(f.engine,f.doc,f.map,400,240,nullptr,nullptr,&sentinel);
        f.m.armed=false;
        c.check(16,s==(action<=2?AETHER_ERROR_INVALID_STATE:AETHER_OK));
        c.check(17,action<=2?sentinel==prior:sentinel && sentinel!=prior);
        if(s==AETHER_OK && sentinel!=prior){paint(f,sentinel,B12);aether_layout_result_destroy(sentinel);}
    }
    c.check(15,f.m.fired==1);c.check(2,f.m.calls>0);
    scan(f,old.value,B12,true,0,content,12);
    f.dom(action==1?"Changed7":"DemoSecret42");
    // Restore the synthetic fixture and refresh revisions, then reuse the same
    // engine/builder after failure or release. Do not retain borrowed callbacks.
    c.api(aether_element_set_attribute(f.node,"value","DemoSecret42"));
    c.api(aether_element_set_attribute(f.node,"type","password"));
    f.m.expected=B12;f.refresh();auto recovered=f.layout();paint(f,recovered,B12);f.dom("DemoSecret42");c.check(19,true);
}
static bool identity(const char* requested) {
    if(!requested || !*requested)return true;
#if defined(__unix__) || defined(__APPLE__)
    Dl_info info{};
    if(!dladdr(reinterpret_cast<void*>(&aether_doc_create),&info) || !info.dli_fname)return false;
    char* actual=realpath(info.dli_fname,nullptr);char* expected=realpath(requested,nullptr);
    const bool same=actual && expected && std::strcmp(actual,expected)==0;
    std::free(actual);std::free(expected);return same;
#else
    return false; // Never silently skip a requested loaded-library identity check.
#endif
}
} // namespace
int main(int argc,char** argv) {
    if(argc>2 || !identity(argc==2?argv[1]:std::getenv("AETHER_EXPECT_LIBRARY"))) {
        std::puts("INFRA loaded-library identity mismatch/unsupported; no cases executed");return 2;
    }
    std::printf("password-public expected_cases=138 ndebug=%d identity=%s\n",
#ifdef NDEBUG
        1,
#else
        0,
#endif
        (argc==2 || std::getenv("AETHER_EXPECT_LIBRARY"))?"verified":"not-requested");
    for(bool ctx:{false,true})for(const auto& r:unicode_rows)
        run("u",r.name,ctx,[&](Case& c){basic(c,ctx,r.input,r.output);});
    for(unsigned i:{3u,6u}){const auto& r=unicode_rows[i];run("cb",r.name,true,[&](Case& c){basic(c,true,r.input,r.output,"password",nullptr,"",true,true,0,12,true);});}
    const char* types[]={"password","PaSsWoRd","PASSWORD","text",nullptr,"","unknown"," password"};
    for(bool ctx:{false,true})for(unsigned i=0;i<8;++i){char n[4];std::snprintf(n,sizeof(n),"%u",i);run("type",n,ctx,[&](Case& c){
        basic(c,ctx,"DemoSecret42",i<3?B12:"DemoSecret42",types[i],nullptr,"",i<3);
        if(i==7){basic(c,ctx,"DemoSecret42","DemoSecret42","password ",nullptr,"",false);basic(c,ctx,"DemoSecret42","DemoSecret42","passwordx",nullptr,"",false);}
    });}
    for(bool ctx:{false,true})for(unsigned i=0;i<4;++i){char n[4];std::snprintf(n,sizeof(n),"%u",i);run("empty",n,ctx,[&](Case& c){
        if(i==0)basic(c,ctx,nullptr,"Type here","password","Type here");
        if(i==1)basic(c,ctx,"","TYPE HERE","password","Type here","input{text-transform:uppercase;}");
        if(i==2)basic(c,ctx,"","","password");
        if(i==3)basic(c,ctx,"","\xc3\xa9","password","\xc3\xa9");
    });}
    const char* transforms[]={"none","uppercase","lowercase","capitalize"};
    const char* ordinary[]={"DemoSecret42","DEMOSECRET42","demosecret42","DemoSecret42"};
    for(bool ctx:{false,true})for(unsigned i=0;i<4;++i)run("transform",transforms[i],ctx,[&](Case& c){
        const auto css=std::string("input{text-transform:")+transforms[i]+";}";
        basic(c,ctx,"DemoSecret42",B12,"password",nullptr,css);
        basic(c,ctx,"DemoSecret42",ordinary[i],"text",nullptr,css,false);
    });
    for(bool ctx:{false,true})for(unsigned i=0;i<4;++i){char n[4];std::snprintf(n,sizeof(n),"%u",i);run("geometry",n,ctx,[&](Case& c){
        const std::string css=std::string("input{font-size:")+(i==1?"24":"12")+"px;width:"+(i==2?"20":"140")+
            "px;text-shadow:1px 2px 0 red,3px 4px 0 blue;}";
        basic(c,ctx,"DemoSecret42",B12,"password",nullptr,css,true,i!=3,2,i==1?24.f:12.f,false,false,-1,i==2?20.f:140.f);
    });}
    const char* containers[]={"#container{display:block;}input{display:block;height:auto;}",
        "#container{display:block;}input{display:inline-block;height:40px;}",
        "#container{display:flex;flex-direction:row;}input{height:auto;align-self:flex-start;}",
        "#container{display:flex;flex-direction:column;}input{height:40px;}"};
    for(bool ctx:{false,true})for(unsigned i=0;i<4;++i){char n[4];std::snprintf(n,sizeof(n),"%u",i);run("container",n,ctx,[&](Case& c){
        basic(c,ctx,"DemoSecret42",B12,"password",nullptr,containers[i],true,true,0,12,false,true,i==0?19.f:-1);
    });}
    for(bool ctx:{false,true})run("sequence","retention",ctx,[&](Case& c){sequence(c,ctx);});
    for(bool ctx:{false,true})for(bool caret:{false,true})for(int action:{1,2}) {
        char name[16];std::snprintf(name,sizeof(name),"%s-%s",caret?"caret":"layout",action==1?"value":"type");
        run("mutate",name,ctx,[&](Case& c){callback_case(c,ctx,caret,action);});
    }
    for(bool ctx:{false,true})for(bool caret:{false,true})run("release",caret?"layout":"map",ctx,[&](Case& c){callback_case(c,ctx,caret,caret?4:3);});
    std::printf("SUMMARY cases=%u expected=138 failed=%u blocked=%u checks=%llu failures=%llu omitted_details=%llu\n",
        cases,failed_cases,blocked_cases,checks,failures,failures-details);
    if(cases!=138 || blocked_cases)return 2;
    return failed_cases?1:0;
}
