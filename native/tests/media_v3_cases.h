#pragma once
#include <string>
#include <vector>
#include <utility>

// Expectations come from media-invalid-design-v3.md, not the implementation:
// recognized length features accept Length or exactly-zero Number; all other
// structurally valid values are U. U AND F=F, U OR T=T, NOT U=U.
namespace media_v3 {
struct query_case { std::string query; bool matches; int unknown; bool syntax = false; };
inline std::vector<query_case> cases(float width, float height) {
    std::vector<query_case> out;
    const std::string t="(min-width:100px)", f="(max-width:100px)";
    const char* features[]={"width","min-width","max-width","height","min-height","max-height"};
    const char* unknown[]={"banana","auto","100","1e2","calc(1px + 1)","calc(1)",
        "0x10px","0x1p3px","calc(0x10px)","calc(0x1p3px)","1.px",
        "calc(--1px)","calc(+ +1px)","calc(- -1px)","1 px","var(--x)",
        "calc(1px * 1px)","calc(1px / 0)","calc(1px+ 1px)",
        "calc(16777217 - 16777216)","calc(1e-30 * 1e-30)","1e-46",
        "1e-400","calc(1e-300 * 1e-300)","1e400","calc(1e300 * 1e300)",
        "0%","50%","calc(50% - 50% + 100px)","calc(0 * 50% + 100px)",
        "\"]\"","url(\"]\")", "url(a\\)b)", "url(a]b)",
        "\"a\\\"b\"", "\"a\\\nb\"",
        // Exact independent string-escape probes. CSS hex escapes consume
        // one optional whitespace terminator, with CRLF treated as one.
        "\"\\61\n\"", "\"\\000061\n\"", "url(\"\\61\n\")",
        "\"\\61\r\n\"", "\"\\61\f\"",
        "\"\\6\n\"", "\"\\061\n\"", "\"\\0061\n\"", "\"\\00061\n\"",
        "\"\\22 content\"", "\"\\000022content\""};
    for (auto feature:features) {
        for (auto value:unknown) {
            const std::string u="("+std::string(feature)+":"+value+")";
            const std::pair<std::string,bool> combos[]={
                {u,false},{"not "+u,false},{"not screen and "+u,false},
                {u+" and "+t,false},{t+" and "+u,false},
                {"not screen and "+u+" and "+t,false},{"not screen and "+t+" and "+u,false},
                {u+" and "+f,false},{f+" and "+u,false},
                {"not screen and "+u+" and "+f,true},{"not screen and "+f+" and "+u,true},
                {u+", "+t,true},{t+", "+u,true},{u+", "+f,false},{f+", "+u,false},
                {"not print and "+u,true},{"print and "+u,false}};
            for (const auto& c:combos) out.push_back({c.first,c.second,1});
        }
        const bool vertical=std::string(feature).find("height")!=std::string::npos;
        const float actual=vertical?height:width;
        const std::pair<std::string,float> valid[]={
            {"100px",100},{"100PX",100},{"100pX",100},
            {"10em",160},{"10EM",160},{"10eM",160},
            {"10rem",160},{"10REM",160},{"10rEm",160},
            {"10vw",width*.1f},{"10VW",width*.1f},{"10vW",width*.1f},
            {"10vh",height*.1f},{"10VH",height*.1f},{"10vH",height*.1f},
            {"0",0},{"+0",0},{"-0",0},{"0.0",0},{".0",0},{"0e2",0},{"0e+2",0},{"0e-46",0},
            {"calc(0)",0},{"calc(1 - 1)",0},{"calc(0 * 10px)",0},
            {"calc(+100PX)",100},{"calc(-1px)",-1},{"calc(2 * -1px)",-2},
            {"calc((50px + 50px) / 2)",50},{"calc(10EM + 1px)",161},
            {"calc(10REM + 1px)",161},{"calc(10VW + 1px)",width*.1f+1},
            {"calc(10VH + 1px)",height*.1f+1},
            {std::to_string(static_cast<int>(actual))+"px",actual},
            {std::to_string(static_cast<int>(actual+100))+"px",actual+100}};
        for (const auto& v:valid) {
            const std::string feature_s=feature;
            const bool match=feature_s.find("min-")==0?actual>=v.second:
                feature_s.find("max-")==0?actual<=v.second:actual==v.second;
            const std::string q="("+feature_s+":"+v.first+")";
            out.push_back({q,match,0}); out.push_back({"not screen and "+q,!match,0});
        }
    }
    // Each malformed alternative is closed before the separator. A comma
    // trapped inside an open block is deliberately tested only in the AST suite.
    const char* malformed[]={"not print and (min-width:50%])",
        "not print and (min-width:calc(1px]))", "not print and (min-width:url(a b))",
        "not print and (min-width:\"bad\nstring)","not print and",
        "not print and and (min-width:100px)","not print (min-width:100px)",
        "not print and (min-width:100px) trailing","not print and (min-width:100px))",
        "not print and (min-width:100px) and",
        "not print and (min-width:url(a\"b))",
        "not print and (min-width:url(a'b))",
        "not print and (min-width:url(a(b))",
        "not print and (min-width:url(a\\\nb))",
        "not print and (min-width:url(a\x01" "b))",
        // Seven digits leave one ordinary character before LF; a consumed
        // space cannot also consume the following LF. Both are bad-string.
        "not print and (min-width:\"\\0000061\nx)",
        "not print and (min-width:\"\\61 \nx)"};
    for (auto q:malformed) {
        out.push_back({q,false,0,true});
        out.push_back({std::string(q)+", "+t,true,0,true});
        out.push_back({t+", "+q,true,0,true});
    }
    out.push_back({"(min-width:100px /* % ] , */)",true,0});
    out.push_back({"(min-width:calc(50% /* cancel */ - 50% + 100px))",false,1});
    return out;
}
}
