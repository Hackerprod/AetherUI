#define main capture_entry_unused
#include "main.cpp"
#undef main
#include <cassert>
static long allocation_budget=-1;
void* operator new(size_t n){if(allocation_budget==0)throw std::bad_alloc();if(allocation_budget>0)--allocation_budget;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
int main(){
 style_property_registry_init();size_t checks=0;
 const std::string css="/*é*/ #absent{writing-mode:vertical-rl}canvas{writing-mode:vertical-rl}";
 Objects o;assert(aether_doc_create(&o.doc)==AETHER_OK);assert(aether_doc_parse_html(o.doc,"<canvas></canvas>")==AETHER_OK);
 h11::Proof p(o.doc);h11::SourceMap map;auto kept=p.retained(css,&map);assert(p.omitted==1);++checks;
 try{h3::authored(kept,true);assert(false);}catch(Failure f){size_t a,z;assert(map.locate(f.begin,f.end,a,z));assert(a==css.rfind("writing-mode"));assert(css.substr(a,z-a)=="writing-mode:vertical-rl");checks+=3;}
 auto analysis=h5::analyze("@media print{canvas{width:20px}}",true);assert(analysis.valid&&analysis.declarations[0].selector_begin==13);++checks;
 h5::WorkBudget b;b.tokens=1;auto limited=h5::analyze(css,true,&b);assert(!limited.valid&&limited.limit_failure);++checks;
 auto malformed=h5::analyze("canvas{width:'",true);assert(!malformed.valid&&malformed.lexical_failure);++checks;
 // Observation allocation failure cannot replace original capture failure.
 logstream.open("/tmp/h16-core-diagnostics.log");
 size_t failures=0;for(long i=0;i<64;++i){allocation_budget=i;h16::failure("style-block-0",css,Failure{"CANVAS_AUTHORED_PROPERTY_UNSUPPORTED",css.rfind("writing-mode"),css.size()-1});allocation_budget=-1;++failures;}
 for(int i=0;i<300;++i)h16::failure("style-block-0",css,Failure{"CANVAS_AUTHORED_CSS_UNKNOWN"});
 logstream.close();assert(failures==64);++checks;
 std::ifstream f("/tmp/h16-core-diagnostics.log");std::string log((std::istreambuf_iterator<char>(f)),{});assert(log.find("diagnostic_v1_incomplete=ALLOCATION_OR_OUTPUT_FAILURE")!=std::string::npos);++checks;
 assert(log.find("vertical-rl")==std::string::npos);++checks;
 auto cap=log.find("diagnostic_v1_incomplete=RECORD_LIMIT");assert(cap!=std::string::npos&&log.find("diagnostic_v1_incomplete=RECORD_LIMIT",cap+1)==std::string::npos);++checks;
 auto frame=h5::analyze("@keyframes k{from{gap:2px}to{gap:3px}}",true);assert(frame.valid&&frame.declarations[0].keyframe&&frame.declarations[0].selector_begin==13);++checks;
 std::cout<<"H16 core checks="<<checks<<" allocation probes="<<failures<<"\n";
}
