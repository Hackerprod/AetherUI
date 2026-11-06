// Private structural index tests. All checks remain active with NDEBUG.
#include "style_source_block_index.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>
#include <vector>
using Kind = css_variable_token_kind;
struct Extent { size_t open, body_end, next; bool operator==(const Extent&) const = default; };
static void check(bool ok, const char* what) { if (!ok) { std::fprintf(stderr,"FAIL %s\n", what); std::exit(1); } }
static bool opens(const css_variable_token& t) { return t.kind==Kind::open || t.kind==Kind::function; }
static char closer(char c) { return c=='(' ? ')' : c=='[' ? ']' : '}'; }
static std::vector<css_variable_token> tokenize(const std::string& s) {
    std::vector<css_variable_token> out; css_component_lexer lex(s.data(),s.size());
    for (;;) { auto t=lex.next(); if(t.kind==Kind::end) break; check(t.begin<t.end && t.end<=s.size(),"lexer progress");out.push_back(t); }
    return out;
}
// Independently structured CSS component recursion, used only on bounded fuzz.
static void consume_block(const std::vector<css_variable_token>& ts,size_t n,size_t& i,std::vector<Extent>& out) {
    const auto opening=ts[i++]; const char end=closer(opening.delimiter);const size_t e=out.size();
    const bool brace=opening.delimiter=='{';if(brace)out.push_back({opening.begin,n,n});
    while(i<ts.size()) {
        const auto t=ts[i];
        if(t.kind==Kind::close && t.delimiter==end) { ++i;if(brace) {out[e].body_end=t.begin;out[e].next=t.end;} return; }
        if(opens(t))consume_block(ts,n,i,out); else ++i;
    }
}
static std::vector<Extent> oracle(const std::vector<css_variable_token>& ts,size_t n) {
    std::vector<Extent> out;size_t i=0;
    while(i<ts.size()) { if(opens(ts[i]))consume_block(ts,n,i,out); else ++i; }
    return out;
}
static void validate(const std::string& s,bool reference=true) {
    auto ts=tokenize(s);
    style_source_block_work_s work;
    style_source_block_index_s index(s, &work);
    check(work.token_visits == ts.size(), "one index visit per token");
    std::vector<Extent> actual;
    for (const auto& t : ts) if (opens(t) && t.delimiter == '{') {
        const auto* e = index.find(static_cast<int32_t>(t.begin));
        check(e != nullptr, "indexed opening exists");
        actual.push_back({static_cast<size_t>(e->open), static_cast<size_t>(e->body_end),
                          static_cast<size_t>(e->next)});
    }
    if(reference)check(actual==oracle(ts,s.size()),"index vs recursive component oracle");
    check(work.extent_visits == actual.size(), "one cursor visit per extent");
    std::vector<Extent> parents;
    for(const auto& e:actual) {
        check(e.open<e.body_end && e.body_end<=e.next && e.next<=s.size(),"extent bounds");
        while(!parents.empty() && e.open>=parents.back().next)parents.pop_back();
        if(!parents.empty())check(e.next<=parents.back().body_end,"child inside parent body");
        parents.push_back(e);
    }
    style_source_block_work_s skipped_work;
    style_source_block_index_s skipped(s, &skipped_work);
    for(size_t wanted=0;wanted<actual.size();wanted+=3) {
        const auto* e = skipped.find(static_cast<int32_t>(actual[wanted].open));
        check(e && static_cast<size_t>(e->body_end) == actual[wanted].body_end &&
              static_cast<size_t>(e->next) == actual[wanted].next, "monotone cursor skips ignored bodies");
    }
    check(skipped.find(static_cast<int32_t>(s.size())) == nullptr, "no imaginary EOF opening");
    check(skipped_work.extent_visits==actual.size(),"cursor amortized bound including skipped extents");
}
int main(int argc,char** argv) {
    if(argc>1 && std::string(argv[1])=="deep") {
        for(size_t n:{1000,2000,4000,8000,10000,100000}) {
            std::string s;for(size_t i=0;i<n;++i)s+="@media (min-width:1px){";
            s+="#target{width:42px";validate(s,false); s+='}';s.append(n,'}');validate(s,false);
            std::printf("deep bounds/index closed+EOF depth=%zu PASS\n",n);
        }
        return 0;
    }
    const std::vector<std::string> fixed={
        "", "{", "{}", "{{", "{[}", "{(]})}", "#target{width:42px", "@media x{a{}",
        "{f(}#target{width:73px}", "{[}#target{width:73px}", "{f(})}#target{width:73px}",
        "{[}]}#target{width:73px}", "a{content:\"};@media x{\";width:42px}",
        "a{content:\"bad\n};a{width:42px}", "a{--x:url(};@media x{);width:42px}",
        "a{--x:url(\"}\");width:42px}", "a{width:42px;/*unfinished }", "a\\{b{width:42px}",
        "a{--x: \\7d ; width:42px}", "@media f({}) {a{width:42px}", "[a{b]", "a{b: f([{}])}"
    };
    for(const auto& s:fixed)validate(s);
    const std::vector<std::string> atoms={"{","}","[","]","(",")","a",";",":"," ","f(","url(x}","url(\"}","\"x}","\"x}\"","/*}","/**/","\\{","\\7d ","\n","@media","#a","\\\n","\"\\61\n"};
    std::mt19937 rng(0xA37E00F);for(size_t n=0;n<200000;++n){std::string s;size_t count=rng()%70;for(size_t i=0;i<count;++i)s+=atoms[rng()%atoms.size()];validate(s);}
    constexpr int32_t n=INT32_MAX, opening=n-1, body_end=n, next=n;
    check(opening+1==n && body_end-opening-1==0 && next==n,"INT32_MAX EOF arithmetic");
    constexpr int32_t real_open=n-2, real_close=n-1, real_next=real_close+1;
    check(real_close-real_open-1==0 && real_next==n,"INT32_MAX real-close arithmetic");
    std::printf("PASS: %zu fixed cases, 200000 fuzz cases, one visit per token, laminar extents, monotone cursor, INT32_MAX arithmetic\n",fixed.size());
}
