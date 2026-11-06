#include "style_runtime_types.h"
#include "media_v3_cases.h"
#include <cstdio>

namespace {
struct diagnostics { int unknown=0, syntax=0; };
void diagnostic(const char* code,const char*,const char*,const char*,void* data) {
    auto& d=*static_cast<diagnostics*>(data);
    if (std::string(code)=="AUIMED003") ++d.unknown;
    else ++d.syntax;
}
}
int main() {
    int total=0, failures=0;
    for (float width:{320.f,800.f}) {
        aether_style_environment_t env{width,600,1,16,0,0,0};
        auto cases=media_v3::cases(width,600);
        // CSS component blocks close at EOF in this media parser entrypoint.
        // Commas inside an open block are not list OR. Stylesheet block
        // extraction has separate typed-boundary and EOF regression coverage.
        cases.push_back({"(min-width:100px",true,0});
        cases.push_back({"(min-width:calc(100px",true,0});
        cases.push_back({"not print and (min-width:banana",true,1});
        cases.push_back({"(min-width:banana, (min-width:100px)",false,1});
        cases.push_back({"(min-width:banana) and (height:banana)",false,2});
        // CSS tokenization consumes EOF as the end of a string or unquoted
        // URL token, without manufacturing bad-string/bad-url tokens.
        cases.push_back({"(min-width:\"unfinished",false,1});
        cases.push_back({"(min-width:\"\\61\n",false,1});
        cases.push_back({"not print and (min-width:\"\\61\n",true,1});
        cases.push_back({"not print and (min-width:\"unfinished",true,1});
        cases.push_back({"(min-width:url(unfinished",false,1});
        cases.push_back({"not print and (min-width:url(unfinished",true,1});
        for (const auto& c:cases) {
            ++total;
            diagnostics d;
            auto* n=media_condition_parse(c.query.c_str(),diagnostic,&d,0);
            const bool actual=media_condition_matches(n,&env);
            // Syntax failures must have a distinct diagnostic; do not require
            // a particular code or count, nor stylesheet callback propagation.
            const bool diag_ok=c.syntax?(d.syntax>0 && d.unknown==0):
                (d.unknown==c.unknown && d.syntax==0);
            const bool preserves_unknown=c.query!="(min-width:banana)" ||
                (n && n->kind==media_condition_s::kind_t::unknown);
            if (actual!=c.matches || !diag_ok || !preserves_unknown) {
                ++failures;
                std::fprintf(stderr,"FAIL AST viewport=%.0f query=%s expected=%d actual=%d unknown=%d/%d syntax=%d required=%d\n",
                    width,c.query.c_str(),c.matches,actual,d.unknown,c.unknown,d.syntax,c.syntax);
            }
            media_condition_destroy(n);
        }
    }
    std::printf("MEDIA V3 AST observations=%d failures=%d\n",total,failures);
    return failures?1:0;
}
