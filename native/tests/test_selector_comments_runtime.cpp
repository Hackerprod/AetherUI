// Public ABI integration. No assert: checks and actions remain active with NDEBUG.
#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
int checks=0, cases=0, failures=0;
void check(bool ok,const std::string& why) { ++checks; if(!ok) throw std::runtime_error(why); }
void near(float a,float b,const char* label) { check(std::fabs(a-b)<.0001f,std::string(label)+": "+std::to_string(a)+" expected "+std::to_string(b)); }
void run(const char* name,const std::function<void()>& fn) { ++cases; try { fn(); } catch(const std::exception& e) { ++failures; std::fprintf(stderr,"FAIL %s: %s\n",name,e.what()); } }
struct Value { float opacity,width,top,right,bottom,left; };
struct Fixture {
    aether_doc_t doc=nullptr; aether_style_runtime_t rt=nullptr;
    std::map<aether_node_t,Value> values;
    std::set<aether_node_t> authored;
    std::map<aether_node_t,std::map<std::string,float>> generated;
    Fixture(const char* html="<body><div id='host' class='card active' data-on='yes'><i id='child' class='active'></i></div><i id='sibling' class='active'></i></body>") {
        try {
            check(aether_doc_create(&doc)==AETHER_OK,"create document");
            check(aether_doc_parse_html(doc,html)==AETHER_OK,"parse document"); collect(reinterpret_cast<aether_node_t>(doc));
            aether_style_runtime_options_t opts{}; aether_style_environment_t env{800,600,1,16,0,0,0};
            check(aether_style_runtime_create(&opts,&env,nullptr,nullptr,&rt)==AETHER_OK,"create runtime");
            sheet("*{opacity:.2;width:10px}",1);
        } catch(...) { cleanup(); throw; }
    }
    ~Fixture(){cleanup();} Fixture(const Fixture&)=delete; Fixture& operator=(const Fixture&)=delete;
    void cleanup(){if(rt)aether_style_runtime_destroy(rt); if(doc)aether_doc_destroy(doc);rt=nullptr;doc=nullptr;}
    void collect(aether_node_t n){authored.insert(n);for(int i=0;i<aether_node_get_child_count(n);++i)collect(aether_node_get_child(n,i));}
    aether_node_t node(const char* id){auto n=aether_doc_get_element_by_id(doc,id);check(n!=nullptr,id);return n;}
    void set(aether_node_t n,const char* key,const char* val){check(aether_element_set_attribute(n,key,val)==AETHER_OK,"set attribute");}
    void sheet(const std::string& css,int expected,int layer=2){int64_t id=0;int32_t rules=-1;check(aether_style_runtime_add_stylesheet(rt,css.c_str(),layer,"comments.css",&id,&rules)==AETHER_OK,"add stylesheet");check(rules==expected,"rules "+std::to_string(rules)+" expected "+std::to_string(expected));}
    void resolve(aether_node_t changed=nullptr){
        aether_style_update_t* updates=nullptr;auto root=reinterpret_cast<aether_node_t>(doc);
        int count=changed?aether_style_runtime_resolve_state_changes(rt,root,&changed,1,&updates):aether_style_runtime_resolve_subtree(rt,root,&updates);
        check(count>=0,"resolve");generated.clear();
        try {for(int i=0;i<count;++i){aether_node_t n=nullptr;aether_computed_style_handle_t h=nullptr;
            auto status=aether_style_runtime_get_update(updates[i],&n,&h,nullptr);
            if(status!=AETHER_OK||!h){if(h)aether_computed_style_handle_destroy(h);check(false,"get update");}
            aether_computed_style_t s{};status=aether_computed_style_copy(h,&s);aether_computed_style_handle_destroy(h);check(status==AETHER_OK,"copy style");
            if(authored.count(n))values[n]={s.opacity,s.width.value,s.margin.top.value,s.margin.right.value,s.margin.bottom.value,s.margin.left.value};
            else {auto parent=aether_node_get_parent(n);if(authored.count(parent)){char* text=aether_node_get_text_content(aether_node_get_child(n,0));generated[parent][text?text:""]=s.opacity;aether_string_free(text);}}
            aether_computed_style_free(&s);
        }}catch(...){aether_style_updates_free(updates,count);throw;}aether_style_updates_free(updates,count);
    }
    Value at(aether_node_t n){check(values.count(n)==1,"style exists");return values.at(n);}
    void expect(aether_node_t n,float opacity){near(at(n).opacity,opacity,"opacity");}
};
}
int main(){
    run("two nested media and structural comments",[]{Fixture f;auto h=f.node("host"),c=f.node("child"),s=f.node("sibling");
        f.sheet("/* ) ] { , */ @media (min-width: 600px) { /* { */ @media (max-width: 900px) { .card/* } ] ) , */.active{opacity:.8} /* } */ } } /* eof",1);
        f.resolve();f.expect(h,.8f);f.expect(c,.2f);f.expect(s,.2f);
        f.sheet("@media (min-width:600px){@media (max-width:700px){.card/**/.active{opacity:.9}}}",1);f.resolve();f.expect(h,.8f);
    });
    run("real whitespace still selects descendants",[]{Fixture f;f.sheet(".card/**/ .active{opacity:.7}",1);f.resolve();f.expect(f.node("host"),.2f);f.expect(f.node("child"),.7f);f.expect(f.node("sibling"),.2f);});
    run("class ID attribute and state mutations",[]{Fixture f;auto h=f.node("host"),c=f.node("child"),s=f.node("sibling");
        f.sheet(".card/**/.active/**/#host/**/[data-on=yes]/**/:hover{opacity:.8}",1);
        aether_element_set_state(h,AETHER_STATE_HOVER);f.resolve();f.expect(h,.8f);f.expect(c,.2f);f.expect(s,.2f);
        f.set(h,"class","card");f.resolve();f.expect(h,.2f);f.set(h,"class","card active");f.resolve();f.expect(h,.8f);
        f.set(h,"id","changed");f.resolve();f.expect(h,.2f);f.set(h,"id","host");f.resolve();f.expect(h,.8f);
        f.set(h,"data-on","no");f.resolve();f.expect(h,.2f);f.set(h,"data-on","yes");f.resolve();f.expect(h,.8f);
        aether_element_set_state(h,AETHER_STATE_NONE);f.resolve(h);f.expect(h,.2f);
        aether_element_set_state(h,AETHER_STATE_HOVER);f.resolve(h);f.expect(h,.8f);f.expect(c,.2f);f.expect(s,.2f);
    });
    run("declarations preserve control semantics",[]{
        const std::string body="{--size:37px;--edges:2px/**/4px;width:var(/**/--size) !/**/important;width:99px;margin:var(--edges);opacity:/**/.7; background-image:url('a/**/b')}";
        Fixture plain,commented;plain.sheet(".card.active"+body,1);commented.sheet(".card/**/.active"+body,1);plain.resolve();commented.resolve();
        auto a=plain.at(plain.node("host")),b=commented.at(commented.node("host"));
        near(a.width,37,"control important var width");near(a.top,2,"control margin top");near(a.right,4,"control margin right");near(a.opacity,.7f,"control opacity");
        near(b.width,a.width,"width parity");near(b.opacity,a.opacity,"opacity parity");near(b.top,a.top,"top parity");near(b.right,a.right,"right parity");near(b.bottom,a.bottom,"bottom parity");near(b.left,a.left,"left parity");
    });
    run("source order and layers across sheets", [] {
        Fixture plain, commented;
        const auto plain_host = plain.node("host");
        const auto commented_host = commented.node("host");
        auto expect_both = [&](float expected) {
            plain.resolve();
            commented.resolve();
            plain.expect(plain_host, expected);
            commented.expect(commented_host, expected);
            near(commented.at(commented_host).opacity, plain.at(plain_host).opacity,
                 "cross-sheet canonical/commented parity");
        };
        plain.sheet(".card.active{opacity:.4}.card.active{opacity:.6}", 2);
        commented.sheet(".card/**/.active{opacity:.4}.card/**/.active{opacity:.6}", 2);
        expect_both(.6f);
        plain.sheet(".card.active{opacity:.9}", 1, 1);
        commented.sheet(".card/**/.active{opacity:.9}", 1, 1);
        expect_both(.6f);
        plain.sheet(".card.active{opacity:.8}", 1);
        commented.sheet(".card/**/.active{opacity:.8}", 1);
        // Equal layer and specificity: the later sheet has global source order.
        expect_both(.8f);
    });
    run("stylesheet EOF comments do not fabricate rules", [] {
        Fixture swallowed;
        swallowed.sheet(".card/*{width:37px}", 0);
        swallowed.resolve();
        near(swallowed.at(swallowed.node("host")).width, 10, "no fabricated rule");

        Fixture completed;
        completed.sheet(".card/**/.active{width:37px}/* unterminated", 1);
        completed.resolve();
        near(completed.at(completed.node("host")).width, 37, "complete rule before EOF");
        near(completed.at(completed.node("child")).width, 10, "child unchanged at EOF");
    });
    run("before after lifecycle",[]{Fixture f("<body><div id='host' class='card active'></div></body>");auto h=f.node("host");
        f.sheet(".card/**/.active:/**/:/**/before{content:'B';opacity:.6}.card/**/.active/**/::after{content:'A';opacity:.8}",2);
        f.resolve();check(f.generated[h].size()==2,"two generated pseudos");near(f.generated[h].at("B"),.6f,"before");near(f.generated[h].at("A"),.8f,"after");check(aether_node_get_child_count(h)==0,"generated excluded from authored children");
        f.set(h,"class","card");f.resolve();check(f.generated[h].empty(),"pseudos removed");f.set(h,"class","card active");f.resolve();check(f.generated[h].size()==2,"pseudos restored");check(aether_node_get_child_count(h)==0,"authored children preserved");
    });
    std::printf("selector comments runtime: %d cases, %d checks, %d failures\n",cases,checks,failures);return failures?1:0;
}
