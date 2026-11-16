#pragma once
#include <string>
#include <vector>
#include <utility>
namespace q2d_cases {
// Literal oracle: pairs are (output byte begin, output byte length) for each
// decoded scalar. No production normalization or expected-output algorithm.
struct Case {const char* name;std::string raw,decoded,normalized;std::vector<std::pair<unsigned,unsigned>> ranges;};
inline std::vector<Case> corpus(){return {
{"empty","","","",{}},
{"A","A","A","A",{{0,1}}},
{"AB","AB","AB","AB",{{0,1},{1,1}}},
{"space"," "," ","",{{0,0}}},
{"tab","\t","\t","",{{0,0}}},
{"lf","\n","\n","",{{0,0}}},
{"two-spaces","  ","  ","",{{0,0},{0,0}}},
{"mixed-only"," \t\n"," \t\n","",{{0,0},{0,0},{0,0}}},
{"lead-space"," A"," A","A",{{0,0},{0,1}}},
{"lead-tab","\tA","\tA","A",{{0,0},{0,1}}},
{"lead-lf","\nA","\nA","A",{{0,0},{0,1}}},
{"trail-space","A ","A ","A",{{0,1},{1,0}}},
{"trail-tab","A\t","A\t","A",{{0,1},{1,0}}},
{"trail-lf","A\n","A\n","A",{{0,1},{1,0}}},
{"around"," A "," A ","A",{{0,0},{0,1},{1,0}}},
{"around-mixed","\tA\n","\tA\n","A",{{0,0},{0,1},{1,0}}},
{"interior-space","A B","A B","A B",{{0,1},{1,1},{2,1}}},
{"interior-tab","A\tB","A\tB","A B",{{0,1},{1,1},{2,1}}},
{"interior-lf","A\nB","A\nB","A B",{{0,1},{1,1},{2,1}}},
{"interior-double","A  B","A  B","A B",{{0,1},{1,1},{1,1},{2,1}}},
{"interior-mixed","A \t\nB","A \t\nB","A B",{{0,1},{1,1},{1,1},{1,1},{2,1}}},
{"two-groups","A\tB\nA","A\tB\nA","A B A",{{0,1},{1,1},{2,1},{3,1},{4,1}}},
{"all-sides"," A  B "," A  B ","A B",{{0,0},{0,1},{1,1},{1,1},{2,1},{3,0}}},
{"entity-tab","A&#9;B","A\tB","A B",{{0,1},{1,1},{2,1}}},
{"entity-lf","A&#10;B","A\nB","A B",{{0,1},{1,1},{2,1}}},
{"hex-tab","A&#x9;B","A\tB","A B",{{0,1},{1,1},{2,1}}},
{"hex-lf","A&#xA;B","A\nB","A B",{{0,1},{1,1},{2,1}}},
{"entity-space","A&#32;B","A B","A B",{{0,1},{1,1},{2,1}}},
{"entity-leading","&#9;A","\tA","A",{{0,0},{0,1}}},
{"entity-trailing","A&#10;","A\n","A",{{0,1},{1,0}}},
{"entity-only","&#9;&#10;","\t\n","",{{0,0},{0,0}}},
{"nbsp","&nbsp;","\xc2\xa0","\xc2\xa0",{{0,2}}},
{"nbsp-around"," &nbsp; "," \xc2\xa0 ","\xc2\xa0",{{0,0},{0,2},{2,0}}},
{"nbsp-interior","A&nbsp;B","A\xc2\xa0" "B","A\xc2\xa0" "B",{{0,1},{1,2},{3,1}}},
{"nbsp-neighbor","A &nbsp; B","A \xc2\xa0 B","A \xc2\xa0 B",{{0,1},{1,1},{2,2},{4,1},{5,1}}},
{"eacute","&#233;","\xc3\xa9","\xc3\xa9",{{0,2}}},
{"eacute-around","\t&#233;\n","\t\xc3\xa9\n","\xc3\xa9",{{0,0},{0,2},{2,0}}},
{"eacute-interior","&#233;\tB","\xc3\xa9\tB","\xc3\xa9 B",{{0,2},{2,1},{3,1}}},
{"two-eacute","&#233;  &#233;","\xc3\xa9  \xc3\xa9","\xc3\xa9 \xc3\xa9",{{0,2},{2,1},{2,1},{3,2}}},
{"amp"," &amp; "," & ","&",{{0,0},{0,1},{1,0}}},
{"quotes","&quot;&#9;&apos;","\"\t'","\" '",{{0,1},{1,1},{2,1}}},
{"literal-eacute","\xc3\xa9 \xc3\xa9","\xc3\xa9 \xc3\xa9","\xc3\xa9 \xc3\xa9",{{0,2},{2,1},{3,2}}}
};}
}
