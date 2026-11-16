#include "../tools/aether_capture_real/report.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace aether::capture;
int main(int argc,char**argv){if(argc!=2)return 2;std::ifstream f(argv[1]);std::string raw((std::istreambuf_iterator<char>(f)),{});auto end=raw.find("publication_schema=");if(end==raw.npos)return 3;raw.resize(end);auto m=parse_metrics(raw);if(!m.document_scope||serialize_metrics(m)!=raw)return 4;unsigned checks=1;
const std::vector<std::pair<std::string,std::string>> bad{{"owners","0"},{"glyph_commands","65536"},{"sample_count","0"},{"mask_hits","18446744073709551615"},{"mask_misses","0"},{"shadow_commands","65537"},{"software_work","50000001"},{"document_elements","16385"},{"document_range_x","nan"},{"extent_width","-1"},{"sample0_width","-1"},{"sample0_baseline_q","0"},{"sample0_first_x","9223372036854775807"},{"raster_work","512000001"},{"prepass_reserved","512000001"},{"main_reserved","512000001"},{"active_pairs","18446744073709551615"}};
for(auto[k,v]:bad){std::string s=raw;auto p=s.find("\n"+k+"=");if(p==s.npos){std::cerr<<"missing key "<<k<<"\n";return 5;}++p;auto q=s.find('\n',p);s.replace(p,q-p,k+"="+v);bool rejected=false;try{(void)parse_metrics(s);}catch(const std::invalid_argument&){rejected=true;}if(!rejected){std::cerr<<"accepted "<<k<<"\n";return 6;}++checks;}
for(auto s:{raw+"unexpected=1\n",raw+"owners=0\n",raw.substr(0,raw.size()-1)}){bool rejected=false;try{(void)parse_metrics(s);}catch(const std::invalid_argument&){rejected=true;}if(!rejected)return 7;++checks;}
std::cout<<"Document report schema "<<checks<<" checks PASS (parser/serializer only, not supervisor tamper injection)\n";
}
