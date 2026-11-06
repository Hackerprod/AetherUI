#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// Public ABI only. Literal expectations are independent of selector lexing.
// Browser startup is unavailable in the validation environment; these are the
// reviewed CSS Syntax/source-derived contracts, not measured browser results.
namespace {
int fixtures = 0, assertions = 0, failures = 0;
void check(bool ok, const std::string& message) {
    ++assertions;
    if (!ok) throw std::runtime_error(message);
}
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    std::map<aether_node_t, float> values;
    Fixture(const char* html = "<body><div id='target'></div><token id='innocent'></token><div id='prefix'></div><div id='suffix'></div></body>") {
        ++fixtures;
        try {
            check(aether_doc_create(&doc) == AETHER_OK, "create document");
            check(aether_doc_parse_html(doc, html) == AETHER_OK, "parse document");
            aether_style_runtime_options_t options{};
            const aether_style_environment_t env{800,600,1,16,0,0,0};
            check(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK, "create runtime");
            sheet("*{opacity:.2}", 1);
        } catch (...) { cleanup(); throw; }
    }
    ~Fixture() { cleanup(); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    void cleanup() {
        if (runtime) aether_style_runtime_destroy(runtime);
        if (doc) aether_doc_destroy(doc);
        runtime = nullptr; doc = nullptr;
    }
    aether_node_t node(const char* id) {
        auto n = aether_doc_get_element_by_id(doc,id);
        check(n != nullptr, std::string("node ") + id); return n;
    }
    void set(aether_node_t n, const char* key, const std::string& value) {
        check(aether_element_set_attribute(n,key,value.c_str()) == AETHER_OK, "set attribute");
    }
    void unchanged(aether_node_t n, const char* key, const std::string& expected) {
        char* got = aether_element_get_attribute(n,key);
        const bool same = got && expected == got;
        aether_string_free(got); check(same, std::string("unchanged DOM ") + key);
    }
    void sheet(const std::string& css, int expected) {
        int64_t id = 0; int32_t rules = -1;
        check(aether_style_runtime_add_stylesheet(runtime,css.c_str(),2,"escapes.css",&id,&rules) == AETHER_OK,"add stylesheet");
        check(rules == expected,"compiled rules expected " + std::to_string(expected) + " got " + std::to_string(rules));
    }
    void resolve(aether_node_t state = nullptr) {
        aether_style_update_t* updates = nullptr;
        auto root = reinterpret_cast<aether_node_t>(doc);
        int count = state ? aether_style_runtime_resolve_state_changes(runtime,root,&state,1,&updates)
                          : aether_style_runtime_resolve_subtree(runtime,root,&updates);
        check(count >= 0,"resolve status");
        try {
            for (int i=0;i<count;++i) {
                aether_node_t n = nullptr; aether_computed_style_handle_t h = nullptr;
                auto status = aether_style_runtime_get_update(updates[i],&n,&h,nullptr);
                if (status != AETHER_OK || !h) {
                    if (h) aether_computed_style_handle_destroy(h);
                    check(false,"get update");
                }
                aether_computed_style_t style{};
                status = aether_computed_style_copy(h,&style);
                aether_computed_style_handle_destroy(h);
                check(status == AETHER_OK,"copy style");
                values[n] = style.opacity; aether_computed_style_free(&style);
            }
        } catch (...) { aether_style_updates_free(updates,count); throw; }
        aether_style_updates_free(updates,count);
    }
    void expect(aether_node_t n, float expected) {
        check(values.count(n) == 1,"computed style exists");
        check(std::fabs(values.at(n)-expected)<.0001f,"opacity expected " + std::to_string(expected) + " got " + std::to_string(values.at(n)));
    }
};
void run(const std::string& label, const std::function<void()>& body) {
    try { body(); } catch(const std::exception& e) { ++failures; std::fprintf(stderr,"FAIL %s: %s\n",label.c_str(),e.what()); }
}
void identity(const std::string& label, const std::string& selector,
              const std::string& actual, const char* key="class", int rules=1, bool matches=true) {
    run(label,[&] {
        Fixture f; auto t=f.node("target"), g=f.node("innocent"), p=f.node("prefix"), s=f.node("suffix");
        f.set(t,key,actual); f.set(p,"class","a comma paren plain"); f.set(s,"class","b token");
        f.set(p,"id","a"); f.set(s,"id","b");
        f.sheet(selector+"{opacity:.8}",rules); f.resolve();
        f.expect(t,matches?.8f:.2f); f.expect(g,.2f); f.expect(p,.2f); f.expect(s,.2f);
        f.unchanged(t,key,actual); f.unchanged(g,"id","innocent");
        f.unchanged(p,"class","a comma paren plain"); f.unchanged(s,"class","b token");
    });
}
void attribute(const std::string& label, const std::string& selector, const std::string& actual, const char* key="data-x", int rules=1, bool matches=true) {
    identity(label,selector,actual,key,rules,matches);
}
void original() {
    struct C { const char *name,*id,*cls,*selector; };
    const C cases[] = {
        {"plain-class", "target", "plain", ".plain"},
        {"colon-simple", "target", "sm:p-4", ".sm\\:p-4"},
        {"slash-simple", "target", "w-1/2", ".w-1\\/2"},
        {"decimal-simple", "target", "p-0.5", ".p-0\\.5"},
        {"brackets-simple", "target", "w-[10px]", ".w-\\[10px\\]"},
        {"comma-simple", "target", "comma,token", ".comma\\,token"},
        {"colon-hex", "target", "sm:p-4", ".sm\\3a p-4"},
        {"digit-id", "123", "item", "#\\31 23"},
        {"hex-ascii", "target", "plain", ".\\70 lain"},
        {"hex-six-digit", "target", "plain", ".\\000070lain"},
        {"unicode-literal", "target", "café", ".café"},
        {"unicode-hex", "target", "café", ".caf\\e9"},
        {"astral-hex", "target", "🚀", ".\\1f680"},
        {"is-escaped-comma", "target", "comma,token", ":is(.comma\\,token)"},
        {"is-escaped-close", "target", "paren)token", ":is(.paren\\)token)"},
        {"escaped-id", "foo:bar", "item", "#foo\\:bar"},
    };
    static_assert(sizeof(cases)/sizeof(cases[0])==16);
    for (const auto& c:cases) run(std::string("sealed/")+c.name,[&] {
        Fixture f; auto t=f.node("target"), g=f.node("innocent"), p=f.node("prefix"), s=f.node("suffix");
        f.set(t,"id",c.id); f.set(t,"class",c.cls); f.sheet(std::string(c.selector)+"{opacity:.8}",1); f.resolve();
        f.expect(t,.8f); f.expect(g,.2f); f.expect(p,.2f); f.expect(s,.2f);
        f.unchanged(t,"id",c.id); f.unchanged(t,"class",c.cls); f.unchanged(g,"id","innocent");
    });
}
void punctuation() {
    struct P { const char *literal,*simple,*hex; };
    const P cases[] = {
        {":",R"(\:)",R"(\3a )"},{"/",R"(\/)",R"(\2f )"},{".",R"(\.)",R"(\2e )"},
        {",",R"(\,)",R"(\2c )"},{"(",R"(\()",R"(\28 )"},{")",R"(\))",R"(\29 )"},
        {"[",R"(\[)",R"(\5b )"},{"]",R"(\])",R"(\5d )"},{"{",R"(\{)",R"(\7b )"},{"}",R"(\})",R"(\7d )"},
        {">",R"(\>)",R"(\3e )"},{"+",R"(\+)",R"(\2b )"},{"~",R"(\~)",R"(\7e )"},
        {"#",R"(\#)",R"(\23 )"},{"*",R"(\*)",R"(\2a )"},{"=",R"(\=)",R"(\3d )"},
        {"\"",R"(\")",R"(\22 )"},{"'",R"(\')",R"(\27 )"},{"\\",R"(\\)",R"(\5c )"}
    };
    for (const auto& p:cases) for (const auto escape:{p.simple,p.hex}) for (const bool id:{false,true}) {
        std::string name=std::string("a")+p.literal+"b", sel=std::string(id?"#":".")+"a"+escape+"b";
        identity("punctuation/"+sel,sel,name,id?"id":"class");
        if (!id) {
            identity("is/"+sel,":is("+sel+")",name);
            identity("where/"+sel,":where("+sel+")",name);
            identity("nested/"+sel,":is(:where("+sel+"),.absent)",name);
            identity("not/"+sel,"#target:not("+sel+")",name,"class",1,false);
        }
    }
    identity("comment-looking",R"(.a\/\*b\*\/c)","a/*b*/c");
    identity("escaped-slashes",R"(.a\\\,b)","a\\,b");
    run("real-comma-parity",[] {
        Fixture f; auto t=f.node("target"), g=f.node("innocent"),p=f.node("prefix"),s=f.node("suffix");
        f.set(t,"class","a\\"); f.sheet(R"(.a\\,token{opacity:.8})",2); f.resolve();
        f.expect(t,.8f); f.expect(g,.8f); f.expect(p,.2f); f.expect(s,.2f);
    });
    run("escaped-then-real-delimiters",[] {
        Fixture f("<body><div id='target' class='a>b'><i id='child' class='child'></i></div><i id='innocent' class='child'></i><i id='safe' class='safe'></i></body>");
        f.sheet(R"(.a\>b > .child,.a\,b,.safe{opacity:.8})",3); f.resolve();
        f.expect(f.node("target"),.2f); f.expect(f.node("child"),.8f); f.expect(f.node("innocent"),.2f); f.expect(f.node("safe"),.8f);
    });
}
void hex_unicode() {
    for (const char* term:{" ","\t","\n","\r","\f","\r\n"}) {
        identity("hex-terminator",std::string("#\\70")+term+"lain","plain","id");
        identity("six-hex-terminator",std::string("#\\000070")+term+"lain","plain","id");
    }
    struct HexCase { const char* first; const char* second; bool literal_valid = true; };
    const HexCase cases[] = {
        {R"(\31 23)","123",false},{R"(\00003123)","123",false},{R"(\000070lain)","plain"},
        {R"(\61b)","\xD8\x9B"},{R"(\61 b)","ab"},{R"(\000061b)","ab"},
        {R"(\80)","\xC2\x80"},{R"(\7ff)","\xDF\xBF"},{R"(\800)","\xE0\xA0\x80"},
        {R"(\d7ff)","\xED\x9F\xBF"},{R"(\e000)","\xEE\x80\x80"},{R"(\ffff)","\xEF\xBF\xBF"},
        {R"(\10000)","\xF0\x90\x80\x80"},{R"(\10ffff)","\xF4\x8F\xBF\xBF"},
        {R"(\10ffff0)","\xF4\x8F\xBF\xBF" "0"},{R"(\10000000)","\xF4\x80\x80\x80" "00"},
        {R"(\e9)","é"},{R"(\1f680)","🚀"},{"\\é","é"}
    };
    for (const auto& c:cases) {
        identity(std::string("hex/")+c.first,std::string("#")+c.first,c.second,"id");
        // A decoded numeric ID is legal in HTML, but its unescaped source is
        // an unrestricted hash rather than an ID selector. Keep both controls.
        identity(std::string("literal/")+c.first,std::string("#")+c.second,c.second,"id",
                 c.literal_valid ? 1 : 0,c.literal_valid);
    }
    for (const char* replacement:{R"(\0)",R"(\d800)",R"(\dfff)",R"(\110000)",R"(\ffffff)"})
        identity("replacement",std::string("#")+replacement,"\xEF\xBF\xBD","id");
    identity("escaped-DEL","#\\7f ","\x7f","id");
    identity("simple-DEL",std::string("#\\")+'\x7f',"\x7f","id");
    identity("raw-DEL",std::string("#")+'\x7f',"\x7f","id",0,false);
    identity("no-normalization",R"(#caf\e9)","café","id",1,false);
    identity("class-case-sensitive",R"(.\50 lain)","plain","class",1,false);
    identity("VT-not-terminator",std::string("#\\61")+'\v'+"b","ab","id",0,false);
    identity("malformed-UTF8",std::string("#a")+'\xe2',"a","id",0,false);
    for (const char* ws:{" ","\t"}) {
        const std::string raw=std::string("#a\\")+ws;
        identity("escaped-final-space",raw,std::string("a")+ws,"id");
        identity("escaped-space-in-function",":is("+raw+")",std::string("a")+ws,"id");
        identity("escaped-space-before-comma",raw+",#missing",std::string("a")+ws,"id",2);
        attribute("escaped-space-attribute",std::string("[data-x=a\\")+ws+"]",std::string("a")+ws);
    }
    for (const char* ws:{"  ","\n\r"}) run("second-whitespace-combinator",[&] {
        Fixture f("<body><div id='parent' class='p'><lain id='target'></lain></div><lain id='innocent'></lain></body>");
        f.sheet(std::string(".\\70")+ws+"lain{opacity:.8}",1); f.resolve();
        f.expect(f.node("parent"),.2f); f.expect(f.node("target"),.8f); f.expect(f.node("innocent"),.2f);
    });
}
void strings_functions() {
    attribute("attribute-name",R"([data\:x=\31 23])","123","data:x");
    attribute("attribute-close",R"([data-x=a\]b])","a]b");
    attribute("attribute-comma",R"([data-x=a\,b])","a,b");
    attribute("attribute-hex-string",R"([data-x="caf\e9"])","café");
    attribute("attribute-empty",R"([data-x=""])","");
    attribute("attribute-quote",R"([data-x="a\"b"])","a\"b");
    attribute("attribute-backslash",R"([data-x="a\\b"])","a\\b");
    attribute("attribute-nested",R"(:is([data-x="a,)]b"]))","a,)]b");
    attribute("attribute-comment-data",R"([data-x="a/*x*/b"])","a/*x*/b");
    attribute("attribute-insensitive",R"([data-x=a\]b i])","A]B");
    attribute("attribute-sensitive",R"([data-x=a\]b s])","A]B","data-x",1,false);
    attribute("attribute-escaped-flag",R"([data-x=a\]b \69])","A]B");
    attribute("attribute-starts",R"([data-x^="a\]b"])","a]btail");
    attribute("attribute-ends",R"([data-x$="a\]b"])","heada]b");
    attribute("attribute-contains",R"([data-x*="a\]b"])","heada]btail");
    attribute("attribute-word",R"([data-x~="a\]b"])","pre a]b post");
    attribute("attribute-dash",R"([data-x|="a\]b"])","a]b-tail");
    identity("escaped-function-name",R"(:\69 s(.a\,b))","a,b");
    identity("unknown-function-escaped-comma",R"(:unknown(.a\,token),.safe)","safe");
    identity("unknown-function-escaped-close",R"(:unknown(.a\)token),.safe)","safe");
    attribute("nested-unquoted-close",R"(:is([data-x=a\)b]))","a)b");
    attribute("nested-unquoted-bracket",R"(:where([data-x=a\]b]))","a]b");
    attribute("nonrecursive-backslash",R"([data-x="\5c 61"])","\\61");

    for (const char* pseudo:{"::before",R"(::\62 efore)"}) run("generated-before-equivalence",[&] {
        Fixture f; auto t=f.node("target"), g=f.node("innocent"), p=f.node("prefix"), q=f.node("suffix");
        f.set(t,"class","a,b");
        f.sheet(std::string(R"(.a\,b)")+pseudo+R"({content:"marker";opacity:.8})",1); f.resolve();
        f.expect(t,.2f); f.expect(g,.2f); f.expect(p,.2f); f.expect(q,.2f);
        int generated=0;
        for (const auto& entry:f.values) if (std::fabs(entry.second-.8f)<.0001f) {
            ++generated; check(entry.first!=t && entry.first!=g && entry.first!=p && entry.first!=q,"only generated node receives pseudo style");
        }
        check(generated==1,"one generated before node with expected style");
        f.unchanged(t,"class","a,b");
    });
    run("escaped-type-start",[] {
        Fixture f("<body><div id='target'></div><token id='innocent'></token></body>");
        f.sheet(R"(\64 IV{opacity:.8})",1); f.resolve(); f.expect(f.node("target"),.8f); f.expect(f.node("innocent"),.2f);
    });
    for (const char* nl:{"\n","\r","\f","\r\n"}) {
        attribute("string-continuation",std::string("[data-x=\"a\\")+nl+"b\"]","ab");
        identity("invalid-identifier-newline",std::string(".bad\\")+nl+"name","badname","class",0,false);
        run("invalid-escape-no-prefix-or-suffix",[&] {
            Fixture f("<body><i id='target' class='badname'></i><i id='prefix' class='bad'></i><name id='suffix' class='name'></name><token id='innocent'></token></body>");
            f.sheet(std::string(".bad\\")+nl+"name{opacity:.8}",0); f.resolve();
            for(const char* id:{"target","prefix","suffix","innocent"}) f.expect(f.node(id),.2f);
        });
        run("has-strict-invalid-escape-rejection",[&] {
            Fixture f("<body><div id='host' class='host'><i id='child' class='safe'></i></div><div id='innocent' class='host'></div></body>");
            f.sheet(std::string(".host:has(.bad\\")+nl+"name,.safe){opacity:.8}",0); f.resolve();
            f.expect(f.node("host"),.2f); f.expect(f.node("child"),.2f); f.expect(f.node("innocent"),.2f);
        });
        for (const char* pseudo:{"is","where","not"}) {
            const bool strict = std::strcmp(pseudo,"not")==0;
            identity("local-identifier-recovery",std::string(":")+pseudo+"(.bad\\"+nl+"x,.safe)","safe","class",strict?0:1,!strict);
            identity("nested-identifier-recovery",std::string(".outer:")+pseudo+"(.bad\\"+nl+"x,.safe)","outer safe","class",strict?0:1,!strict);
            // Intentionally no closing quote: the newline ends the bad string.
            const std::string bad=std::string(":")+pseudo+"([data-x=\"bad"+nl+"],.safe)";
            identity("bad-string-recovery",bad,"safe","class",strict?0:1,!strict);
            identity("outer-bad-string-recovery",".outer"+bad,"outer safe","class",strict?0:1,!strict);
            if (!strict) identity("nested-bad-string-recovery",":is(.outer"+bad+")","outer safe");
        }
        run("later-valid-rule",[&] {
            Fixture f; auto t=f.node("target"),g=f.node("innocent"),p=f.node("prefix"),s=f.node("suffix");
            f.set(t,"class","badname");
            f.sheet(std::string(".bad\\")+nl+"name{opacity:.8}#innocent{opacity:.6}",1); f.resolve();
            f.expect(t,.2f); f.expect(g,.6f); f.expect(p,.2f); f.expect(s,.2f);
        });
    }
    for (const char* escaped:{R"(.a\,b)",R"(.a\)b)",R"(.a\]b)",R"(.a\>b)"}) run(std::string("has/")+escaped,[&] {
        Fixture f("<body><div id='host' class='host'><i id='child'></i></div><div id='innocent' class='host'><token id='guard'></token></div></body>");
        std::string value=escaped; // Fixture identity is specified separately, not decoded by the parser.
        if (value==R"(.a\,b)") value="a,b"; else if(value==R"(.a\)b)") value="a)b";
        else if(value==R"(.a\]b)") value="a]b"; else value="a>b";
        f.set(f.node("child"),"class",value); f.sheet(std::string(".host:has(")+escaped+"){opacity:.8}",1); f.resolve();
        f.expect(f.node("host"),.8f); f.expect(f.node("innocent"),.2f); f.expect(f.node("child"),.2f); f.expect(f.node("guard"),.2f);
        f.unchanged(f.node("child"),"class",value);
    });
}
void cascade_mutation() {
    for (const char* escaped:{R"(.p-0\.5)",R"(.p-0\2e 5)"}) for (bool reverse:{false,true}) run("same-specificity-class",[&] {
        Fixture f; auto t=f.node("target"); f.set(t,"class","p-0.5 equal");
        std::string a=std::string(escaped)+"{opacity:.8}", b=".equal{opacity:.4}";
        f.sheet(reverse?b+a:a+b,2); f.resolve(); f.expect(t,reverse?.8f:.4f);
        f.expect(f.node("innocent"),.2f); f.expect(f.node("prefix"),.2f); f.expect(f.node("suffix"),.2f);
    });
    for (bool reverse:{false,true}) run("canonical-versus-escaped",[&] {
        Fixture f; auto t=f.node("target"); f.set(t,"class","plain");
        std::string a=R"(.\70 lain{opacity:.8})",b=".plain{opacity:.4}";
        f.sheet(reverse?b+a:a+b,2); f.resolve(); f.expect(t,reverse?.8f:.4f);
        f.expect(f.node("innocent"),.2f); f.expect(f.node("prefix"),.2f); f.expect(f.node("suffix"),.2f);
    });
    for (const char* css:{R"(.a\,b{opacity:.8}#target{opacity:.4})",R"(.a\,b{opacity:.8}.a\,b.extra{opacity:.4})",R"(.extra{opacity:.4}:where(.a\,b){opacity:.8})",R"(:is(#never:hover.bad\
x,.a\,b){opacity:.8}.extra{opacity:.4})"}) run("specificity",[&] {
        Fixture f; auto t=f.node("target"); f.set(t,"class","a,b extra"); f.sheet(css,2); f.resolve(); f.expect(t,.4f);
        f.expect(f.node("innocent"),.2f); f.expect(f.node("prefix"),.2f); f.expect(f.node("suffix"),.2f);
    });
    for (bool reverse:{false,true}) run("same-specificity-ID",[&] {
        Fixture f; auto t=f.node("target"); f.set(t,"id","foo:bar");
        const std::string a=R"(#foo\:bar{opacity:.8})", b=R"(#foo\3a bar{opacity:.4})";
        f.sheet(reverse?b+a:a+b,2); f.resolve(); f.expect(t,reverse?.8f:.4f);
        f.expect(f.node("innocent"),.2f); f.expect(f.node("prefix"),.2f); f.expect(f.node("suffix"),.2f);
        f.unchanged(t,"id","foo:bar");
    });
    run("ID-specificity",[] {
        Fixture f; auto t=f.node("target"); f.set(t,"id","foo:bar"); f.set(t,"class","x y");
        f.sheet(R"(#foo\:bar{opacity:.8}.x.y{opacity:.4})",2); f.resolve(); f.expect(t,.8f);
        f.expect(f.node("innocent"),.2f); f.expect(f.node("prefix"),.2f); f.expect(f.node("suffix"),.2f);
    });
    run("mutations-and-incremental-state",[] {
        Fixture f; auto t=f.node("target"),g=f.node("innocent"),p=f.node("prefix"),s=f.node("suffix");
        f.sheet(R"(.a\,b{opacity:.4}.a\,b:\68 over{opacity:.8}#caf\e9{opacity:.6}#caf\e9:hover{opacity:.9})",4);
        f.resolve(); f.expect(t,.2f);
        // Attribute mutations are resolved through full subtree, never falsely
        // presented as state notifications. State-only changes use their ABI.
        f.set(t,"class","a,b"); f.resolve(); f.expect(t,.4f); f.unchanged(t,"class","a,b");
        for (int on:{1,0}) {
            aether_element_set_state(t,on?AETHER_STATE_HOVER:AETHER_STATE_NONE); f.resolve(t); f.expect(t,on?.8f:.4f);
            f.resolve(); f.expect(t,on?.8f:.4f);
        }
        f.set(t,"class","other"); f.resolve(); f.expect(t,.2f);
        f.set(t,"id","café"); f.resolve(); f.expect(t,.6f); f.unchanged(t,"id","café");
        for (int on:{1,0}) {
            aether_element_set_state(t,on?AETHER_STATE_HOVER:AETHER_STATE_NONE); f.resolve(t); f.expect(t,on?.9f:.6f);
            f.resolve(); f.expect(t,on?.9f:.6f);
        }
        f.set(t,"id","target"); f.resolve(); f.expect(t,.2f);
        f.expect(g,.2f); f.expect(p,.2f); f.expect(s,.2f);
    });
    run("has-incremental-hover",[] {
        Fixture f("<body><div id='host' class='host'><i id='child' class='a,b'></i></div><div id='innocent' class='host'><i id='guard' class='a'></i></div></body>");
        f.sheet(R"(.host:has(.a\,b:hover){opacity:.8})",1); f.resolve();
        auto child=f.node("child");
        for (int on:{1,0,1,0}) {
            aether_element_set_state(child,on?AETHER_STATE_HOVER:AETHER_STATE_NONE); f.resolve(child);
            f.expect(f.node("host"),on?.8f:.2f); f.expect(f.node("innocent"),.2f); f.expect(child,.2f); f.expect(f.node("guard"),.2f);
            f.resolve(); f.expect(f.node("host"),on?.8f:.2f);
        }
    });
    std::string name,selector=".";
    for(int i=0;i<2048;++i) { name+="a,"; selector+=R"(\61\2c)"; }
    identity("long-escaped-name",selector,name);
    selector=R"(.a\,b)";
    for(int i=0;i<32;++i) selector=":is("+selector+")";
    identity("nested-balanced",selector,"a,b");
}
} // namespace
int main() {
    original(); punctuation(); hex_unicode(); strings_functions(); cascade_mutation();
    std::printf("SELECTOR_ESCAPES FIXTURES=%d ASSERTIONS=%d FAILURES=%d\n",fixtures,assertions,failures);
    return failures?1:0;
}
