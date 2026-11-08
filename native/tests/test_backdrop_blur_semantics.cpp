#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>

namespace {
unsigned checks = 0, cases = 0;
void need(bool b, const char* m) { ++checks; if (!b) { std::fprintf(stderr,"FAIL blur case %u: %s\n",cases,m); std::exit(1); } }
void ok(aether_status_t s, const char* m) { need(s == AETHER_OK,m); }
struct Diagnostics { int count=0; std::string code, property; };
void diagnostic(const char* code,const char* property,const char*,const char*,void* p) {
    auto& d=*static_cast<Diagnostics*>(p); ++d.count; d.code=code?code:"";d.property=property?property:"";
}
struct Flat {
    aether_computed_style_t s{};
    ~Flat(){aether_computed_style_free(&s);}
    Flat()=default; Flat(const Flat&)=delete;
};
struct Fixture {
    aether_doc_t doc{}; aether_style_runtime_t runtime{}; aether_node_t root{},parent{},node{};
    aether_style_revision_t revision{}; int64_t sheet{}; Diagnostics diagnostics;
    aether_style_environment_t env{200,100,1,16,0,0,0};
    Fixture(){
        ++cases;ok(aether_doc_create(&doc),"doc");
        ok(aether_doc_parse_html(doc,"<html><body><div id='p'><div id='x'></div><div id='s'></div></div></body></html>"),"html");
        root=aether_doc_get_document_element(doc);parent=aether_doc_get_element_by_id(doc,"p");node=aether_doc_get_element_by_id(doc,"x");
        need(root&&parent&&node,"nodes");ok(aether_style_runtime_create(nullptr,&env,diagnostic,&diagnostics,&runtime),"runtime");
    }
    ~Fixture(){aether_style_revision_destroy(revision);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
    void css(const std::string& text){ if(sheet)need(aether_style_runtime_remove_stylesheet(runtime,sheet)==AETHER_TRUE,"remove sheet");
        int32_t rules=0;ok(aether_style_runtime_add_stylesheet(runtime,text.c_str(),0,"blur.css",&sheet,&rules),"sheet");need(rules>0,"nonempty rules"); }
    void in(aether_node_t n,const std::string& text){ok(aether_element_set_attribute(n,"style",text.c_str()),"inline");}
    void resolve(){aether_style_transaction_t tx{};ok(aether_style_runtime_prepare_revision(runtime,root,&env,&tx),"prepare");
        aether_style_revision_t next{};ok(aether_style_runtime_commit_revision(runtime,tx,&next),"commit");aether_style_transaction_destroy(tx);
        aether_style_revision_destroy(revision);revision=next;}
    uint32_t read(Flat& out,aether_node_t n=nullptr,aether_style_revision_t rev=nullptr){
        if(!n)n=node;if(!rev)rev=revision;unsigned found=0;uint32_t impact=0;
        for(int i=0;i<aether_style_revision_style_count(rev);++i){aether_node_t current{};aether_computed_style_handle_t h{};uint32_t im{};
            ok(aether_style_revision_get_style(rev,i,&current,&h,&im),"read revision");
            if(current==n){++found;ok(aether_computed_style_copy(h,&out.s),"copy style");impact=im;}aether_computed_style_handle_destroy(h);}
        need(found==1,"node exactly once");return impact;
    }
    void expect(std::initializer_list<float> values,aether_node_t n=nullptr,aether_style_revision_t rev=nullptr){Flat f;read(f,n,rev);
        need(f.s.backdrop_filters.count==static_cast<int32_t>(values.size()),"filter count");size_t i=0;
        for(float value:values){const auto& op=f.s.backdrop_filters.items[i++];const auto& l=op.length_amount;
            need(op.kind==AETHER_BACKDROP_FILTER_BLUR,"blur operation");need(std::fabs(l.value-value)<.0001f,"independent computed amount");
            need(l.unit==AETHER_CSS_UNIT_PX||l.unit==AETHER_CSS_UNIT_LINEAR,"computed length unit");
            need(l.linear_percent==0&&l.linear_em==0&&l.linear_rem==0&&l.linear_vw==0&&l.linear_vh==0,"no unresolved coefficients");}
    }
};
void grammar(){
    struct Case{const char* value;float expected;bool valid;};
    const Case table[]={
        {"blur()",0,true},{"blur(/*comment*/)",0,true},{"blur(0)",0,true},{"blur(+0)",0,true},{"blur(-0)",0,true},
        {"blur(0e2)",0,true},{"blur(-0px)",0,true},{"blur(+1e-46px)",0,true},{"blur(1px)",1,true},{"blur(1em)",16,true},{"blur(1rem)",16,true},{"blur(1vw)",2,true},{"blur(1vh)",1,true},
        {"blur(calc(2 * 3px))",6,true},{"blur(calc(0px - 1em))",0,true},{"blur(calc(-2px + 1em))",14,true},
        {"blur(calc(1px /*%*/ + 2px))",3,true},{"blur(/*%*/1px)",1,true},
        {"blur(calc(1px /*c*/ + /*c*/ 2px))",3,true},
        {"blur(2)",9,false},{"blur(-1e-46)",9,false},{"blur(-1e-46px)",9,false},{"blur(-1px)",9,false},
        {"blur(10%)",9,false},{"blur(0%)",9,false},{"blur(auto)",9,false},{"blur(calc(0))",9,false},
        {"blur(calc(1 - 1))",9,false},{"blur(calc(1px + 0%))",9,false},{"blur(calc(1px + 10% - 10%))",9,false},
        {"blur(calc(0 * 10%))",9,false},{"blur(calc(0%))",9,false},{"blur(1px,2px)",9,false},
        {"blur(1px 2px)",9,false},{"brightness(1)",9,false},{"blur(1px) blur(0%)",9,false},
        {"none blur(1px)",9,false},{"blur/**/(1px)",9,false},{"blur(calc(1px/**/+/**/2px))",9,false},
        {"blur(calc(1px/*\n*/+/*\n*/2px))",9,false},{"blur(calc(1px +2px))",9,false},
        {"blur(calc(1px+ 2px))",9,false}};
    // Preserve diagnostic frequency: sheets compile once, inline declarations
    // compile on each preparation. Scratch application never adds a diagnostic.
    for(const char* property:{"backdrop-filter","-webkit-backdrop-filter"})for(bool inline_style:{false,true})for(auto c:table){
        Fixture f;const auto declarations=std::string(property)+":blur(9px);"+property+":"+c.value;
        if(inline_style)f.in(f.node,declarations);else f.css("#x{"+declarations+"}");
        f.resolve();f.expect({c.expected});need(f.diagnostics.count==(c.valid?0:1),"one literal diagnostic");
        if(!c.valid)need(f.diagnostics.code=="AETHER-CSS-BACKDROP-FILTER"&&f.diagnostics.property==property,"literal diagnostic origin");
        const int reported=f.diagnostics.count;f.resolve();f.expect({c.expected});need(f.diagnostics.count==reported+(inline_style&&!c.valid?1:0),"literal diagnostic only at its compile phase, never scratch");
    }
    Fixture f;f.css("#x{backdrop-filter:blur() blur(2px) blur(calc(0px - 2em))}");f.resolve();f.expect({0,2,0});
}
void cascade(){
    for(const char* declarations:{
        "-webkit-backdrop-filter:blur(2px);backdrop-filter:blur(7px)",
        "backdrop-filter:blur(2px);-webkit-backdrop-filter:blur(7px)",
        "backdrop-filter:blur(7px)!important;-webkit-backdrop-filter:blur(2px)",
        "-webkit-backdrop-filter:blur(7px)!important;backdrop-filter:blur(2px)",
        "-WEBKIT-BACKDROP-FILTER:blur(2px);BACKDROP-FILTER:blur(7px)",
        "-webkit-backdrop-filter:blur(2px);backdrop-\\66 ilter:blur(7px)"}){
        Fixture f;f.css(std::string("#x{")+declarations+"}");f.resolve();f.expect({7});}
    for(bool reverse:{false,true}){Fixture f;f.css(reverse?"#x{-webkit-backdrop-filter:blur(7px)} div{backdrop-filter:blur(2px)}":"#x{backdrop-filter:blur(7px)} div{-webkit-backdrop-filter:blur(2px)}");f.resolve();f.expect({7});}
    Fixture f;f.css("#x{-webkit-backdrop-filter:blur(2px)}");f.in(f.node,"backdrop-filter:blur(7px)");f.resolve();f.expect({7});
    f.css("#x{-webkit-backdrop-filter:blur(2px)!important}");f.resolve();f.expect({2});
    f.in(f.node,"backdrop-filter:blur(7px)!important");f.resolve();f.expect({7});
    f.in(f.node,"backdrop-filter:initial!important");f.resolve();f.expect({});
    for(bool reverse:{false,true}){
        Fixture invalid;invalid.css(reverse?"#x{backdrop-filter:blur(9px);-webkit-backdrop-filter:blur(-1e-46px)}":"#x{-webkit-backdrop-filter:blur(9px);backdrop-filter:blur(2)}");invalid.resolve();invalid.expect({9});
        invalid.in(invalid.node,reverse?"-webkit-backdrop-filter:none":"backdrop-filter: /*trivia*/ none /*end*/");invalid.resolve();invalid.expect({});
    }
    Fixture sheets;sheets.css("#x{-webkit-backdrop-filter:blur(2px)}");int64_t second{};ok(aether_style_runtime_add_stylesheet(sheets.runtime,"#x{backdrop-filter:blur(7px)}",0,"second",&second,nullptr),"second sheet");sheets.resolve();sheets.expect({7});
}
void variables(){
    for(const char* property:{"backdrop-filter","-webkit-backdrop-filter"}){
        for(bool inline_style:{false,true})for(const char* value:{"2","-1e-46px","calc(0)","calc(1px + 0%)","calc(1px/**/+/**/2px)"}){
            Fixture f;const auto declarations=std::string("--x:")+value+";"+property+":blur(9px);"+property+":blur(var(--x,4px))";
            if(inline_style)f.in(f.node,declarations);else f.css("#x{"+declarations+"}");f.resolve();f.expect({});
            need(f.diagnostics.count==1&&f.diagnostics.code=="AETHER-CSS-BACKDROP-FILTER"&&f.diagnostics.property=="backdrop-filter","deferred diagnostic once under canonical name");const int reported=f.diagnostics.count;f.resolve();f.expect({});need(f.diagnostics.count==reported+1,"one deferred diagnostic per preparation, never scratch");}
        Fixture f;f.in(f.node,std::string(property)+":blur(var(--missing,4px))");f.resolve();f.expect({4});
        for(const char* value:{"initial","unset","inherit"}){f.in(f.parent,"font-size:10px;backdrop-filter:blur(1em)");f.in(f.node,std::string("font-size:30px;--x:")+value+";"+property+":var(--x)");f.resolve();
            // Custom CSS-wide keywords affect the custom declaration itself, not a string substitution.
            f.expect({});}
        f.in(f.node,std::string("font-size:30px;")+property+":inherit");f.resolve();f.expect({10});
        f.in(f.node,std::string(property)+":var(--missing,inherit)");f.resolve();f.expect({10});
        f.in(f.parent,"backdrop-filter:blur()");f.resolve();f.expect({0});
        f.in(f.node,std::string(property)+":var(--missing,initial)");f.resolve();f.expect({});
        f.in(f.node,std::string(property)+":var(--missing,unset)");f.resolve();f.expect({});
        f.in(f.node,std::string("--x:calc(10px - 1em);")+property+":blur(var(--x))");f.resolve();f.expect({0});
        f.in(f.node,std::string("--x:;")+property+":blur(var(--x))");f.resolve();f.expect({0});
    }
}
void comments(){
    for(const char* property:{"backdrop-filter","-webkit-backdrop-filter"}){
        Fixture f;const std::string prefix=std::string(property)+":";
        f.in(f.node,prefix+"blur(calc(1px/**/+/**/2px))");f.resolve();f.expect({});
        f.in(f.node,prefix+"blur(calc(1px    +    2px))");f.resolve();f.expect({3});
        f.in(f.node,prefix+"blur(var(--b));width:var(--b)");
        f.css("#p{--a:calc(1px/**/+/**/2px);--b:var(--a)}");f.resolve();f.expect({});
        {Flat s;f.read(s);need(s.s.width.value==3,"legacy width still receives cleaned custom source");}
        f.css("#p{--a:calc(1px    +    2px);--b:var(--a)}");f.resolve();f.expect({3});
        f.in(f.node,"--a:20px;"+prefix+"blur(var(--b))");f.resolve();f.expect({3}); // Parent --b is terminal.
        f.in(f.parent,"--a:calc(1px/**/+/**/2px);--b:var(--a)");f.resolve();f.expect({});
        f.in(f.parent,"--a:calc(1px    +    2px);--b:var(--a)");f.resolve();f.expect({3});
        f.in(f.node,"--b:calc(2px /*ok*/ + 3px);"+prefix+"blur(var(--b))");f.resolve();f.expect({5});
        f.in(f.node,"--b:calc(2px/**/+/**/3px);"+prefix+"blur(var(--b))");f.resolve();f.expect({});
        f.in(f.node,"--b:blur(calc(2px/**/+/**/3px));"+prefix+"var(--b)");f.resolve();f.expect({});
        f.in(f.node,prefix+"blur(var(--absent,calc(1px/**/+/**/2px)))");f.resolve();f.expect({});
        f.in(f.node,"--left:1px;--right:2px;--op:+;"+prefix+"blur(calc(var(--left) var(--op) var(--right)))");f.resolve();f.expect({3});
        f.in(f.node,"--left:1px;--right:2px;--op:+;"+prefix+"blur(calc(var(--left)/**/var(--op)/**/var(--right)))");f.resolve();f.expect({});
        f.in(f.node,"--a:var(--b);--b:var(--a);"+prefix+"blur(var(--a,4px))");f.resolve();f.expect({4});
        f.in(f.node,"--b:inherit;"+prefix+"blur(var(--b))");f.resolve();f.expect({3});
        f.css("#p{--b:calc(1px/**/+/**/2px)!important;--b:calc(1px + 2px)}");f.in(f.parent,"");f.resolve();f.expect({});
        f.css("#p{--b:calc(1px + 2px)!important;--b:calc(1px/**/+/**/2px)}");f.resolve();f.expect({3});
    }
}
void context_and_snapshots(){
    Fixture f;f.in(f.node,"font-size:16px;backdrop-filter:blur(calc(10px - 1em))");f.resolve();f.expect({0});
    aether_style_revision_t old{};ok(aether_style_runtime_retain_revision(f.runtime,&old),"retain");
    f.in(f.node,"backdrop-filter:blur(calc(10px - 1em));font-size:8px");f.resolve();f.expect({2});f.expect({0},nullptr,old);
    {Flat copy;f.read(copy);copy.s.backdrop_filters.items[0].length_amount.value=99;f.expect({2});}
    f.in(f.node,"font-size:16px;backdrop-filter:blur(calc(-2px + 1em))");f.resolve();f.expect({14});
    f.in(f.node,"font-size:32px;backdrop-filter:blur(calc(-2px + 1em))");f.resolve();f.expect({30});
    f.in(f.node,"backdrop-filter:blur(calc(10px - 1rem))");f.in(f.root,"font-size:16px");f.resolve();f.expect({0});
    f.in(f.root,"font-size:8px");f.resolve();f.expect({2});
    f.in(f.node,"backdrop-filter:blur(calc(30px - 10vw)) blur(calc(15px - 10vh))");f.resolve();f.expect({10,5});
    f.env.viewport_width=400;f.env.viewport_height=200;f.resolve();f.expect({0,0});
    f.env.viewport_width=500;f.env.viewport_height=300;f.resolve();f.expect({0,0});
    {Flat s;need(f.read(s)==0,"context remains clamped: no spurious filter impact");}
    f.env.viewport_width=100;f.env.viewport_height=100;f.resolve();f.expect({20,5});
    f.resolve();{Flat s;need(f.read(s)==0,"no-op has no impact");}
    // A prepared candidate is not published by destruction.
    const auto generation=aether_style_revision_generation(f.revision);aether_style_transaction_t discarded{};
    f.in(f.node,"backdrop-filter:blur(77px)");ok(aether_style_runtime_prepare_revision(f.runtime,f.root,&f.env,&discarded),"prepare discarded");
    aether_style_transaction_destroy(discarded);aether_style_revision_t retained{};ok(aether_style_runtime_retain_revision(f.runtime,&retained),"retain after discard");
    need(aether_style_revision_generation(retained)==generation,"discard preserves generation");f.expect({20,5},nullptr,retained);aether_style_revision_destroy(retained);
    aether_style_transaction_t stale{};ok(aether_style_runtime_prepare_revision(f.runtime,f.root,&f.env,&stale),"prepare stale blur");
    f.in(f.node,"backdrop-filter:blur(88px)");aether_style_revision_t rejected{};need(aether_style_runtime_commit_revision(f.runtime,stale,&rejected)==AETHER_ERROR_INVALID_STATE&&!rejected,"stale blur cannot publish");aether_style_transaction_destroy(stale);
    f.expect({0},nullptr,old);aether_style_revision_destroy(old);
}
void sticky_focal_scope(){
    Fixture f;
    // The unrelated original source forces the focal view, while the empty
    // exponential tree exhausts resolver work, rather than output length.
    std::string css="#p{--marker:calc(1px /*original*/ + 2px);--a0:;";
    for(int i=1;i<=20;++i){const auto previous="var(--a"+std::to_string(i-1)+")";css+="--a"+std::to_string(i)+":"+previous+previous+";";}
    css+="}";f.css(css);f.in(f.node,"--alias:var(--a20,4px);backdrop-filter:blur(var(--alias,7px))");f.resolve();f.expect({});
    f.in(f.node,"--alias:var(--a3,4px);backdrop-filter:blur(var(--alias,7px))");f.resolve();f.expect({0});
    f.in(f.node,"--a20:5px;--alias:var(--a20,4px);backdrop-filter:blur(var(--alias,7px))");f.resolve();f.expect({5});
}
void isolated_legacy_root(){
    Fixture f;f.in(f.parent,"--a:calc(1px /*comment*/ + 2px)");f.in(f.node,"backdrop-filter:blur(var(--a,4px))");f.resolve();f.expect({3});
    aether_style_update_t* updates{};const int n=aether_style_runtime_resolve_subtree(f.runtime,f.node,&updates);need(n>0,"legacy subtree resolution");
    unsigned found=0;
    for(int i=0;i<n;++i){aether_node_t node{};aether_computed_style_handle_t handle{};
        ok(aether_style_runtime_get_update(updates[i],&node,&handle,nullptr),"legacy update");
        if(node==f.node){++found;Flat style;ok(aether_computed_style_copy(handle,&style.s),"copy isolated style");
            need(style.s.backdrop_filters.count==1&&style.s.backdrop_filters.items[0].length_amount.value==4,"isolated focal scope uses fallback");}
        aether_computed_style_handle_destroy(handle);}
    need(found==1,"isolated node changed exactly once");aether_style_updates_free(updates,n);
    // The contextual retain API deliberately rejects a legacy revision.
    aether_style_revision_t isolated{};need(aether_style_runtime_retain_revision(f.runtime,&isolated)==AETHER_ERROR_INVALID_STATE&&!isolated,"legacy revision is not contextual");
    f.expect({3}); // The older contextual snapshot remains independently readable.
}
}
int main(){sticky_focal_scope();isolated_legacy_root();grammar();cascade();variables();comments();context_and_snapshots();std::printf("PASS blur semantics: %u cases, %u active checks\n",cases,checks);}
