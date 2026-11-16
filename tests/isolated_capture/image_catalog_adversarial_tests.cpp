#ifdef NDEBUG
#error assertions required
#endif
// Independently authored admission regression driver.
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cassert>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include "../../native/src/rendering/native_frame.h"
#include "../software_render_smoke/png_writer.h"
namespace fs=std::filesystem;
std::string read(fs::path p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
void write(fs::path p,std::string s){std::ofstream f(p,std::ios::binary);f.write(s.data(),s.size());assert(f.good());}
int run(std::vector<std::string> a){std::vector<char*> av;for(auto&x:a)av.push_back(x.data());av.push_back(nullptr);auto pid=fork();assert(pid>=0);if(!pid){execv(av[0],av.data());_exit(127);}int st;assert(waitpid(pid,&st,0)==pid);return WIFEXITED(st)?WEXITSTATUS(st):128;}
int main(int argc,char**argv){assert(argc==3);fs::path root=argv[2];assert(!fs::exists(root));fs::create_directories(root/"assets");auto png=smoke_png::png(smoke_png::Bytes{255,0,0,255},1,1);write(root/"assets/a.png",{(char*)png.data(),png.size()});
struct Case{std::string name,css,body;bool ok;};std::vector<Case> cases{
{"important","#a{background-image:url(Hero_1)!important}","<div id='a'></div>",true},
{"case-sensitive-alias","#a{background-image:url(hero_1)}","<div id='a'></div>",false},
{"escaped-property","#a{background-\\69mage:url(Hero_1)}","<div id='a'></div>",false},
{"uppercase-property","#a{BACKGROUND-IMAGE:url(Hero_1)}","<div id='a'></div>",false},
{"uppercase-url","#a{background-image:URL(Hero_1)}","<div id='a'></div>",false},
{"space-url","#a{background-image:url( Hero_1 )}","<div id='a'></div>",false},
{"comment-url","#a{background-image:url(Hero_1)/**/}","<div id='a'></div>",true},
{"inactive-missing","@media screen and (min-width:9999px){#a{background-image:url(absent)}}","<div id='a'></div>",false},
{"matched-and-unmatched-missing","#a{background-image:url(Hero_1)}#missing{background-image:url(absent)}","<div id='a'></div>",false},
{"literal-remote","#a{background-image:url(https://example.invalid/a.png)}","<div id='a'></div>",false},
{"literal-file","#a{background-image:url(file:///etc/passwd)}","<div id='a'></div>",false},
{"text-rejected","#a{background-image:url(Hero_1)}","<div id='a'>text</div>",false},
{"inline-important","","<div id='a' style='background-image:url(Hero_1)!important'></div>",true},
{"nested-url-custom","#a{--asset:var(--x,url(Hero_1));background-image:none}","<div id='a'></div>",false}
};
for(auto&c:cases){auto input=root/(c.name+".html"),out=root/c.name;write(input,"<html><head><style>body{margin:0}#a{display:block;width:2px;height:2px}"+c.css+"</style></head><body>"+c.body+"</body></html>");int rc=run({argv[1],"--geometry",input.string(),out.string(),"4","4","1","--image-root",(root/"assets").string(),"--image","Hero_1","a.png"});auto report=read(out/"report.log");if((rc==0)!=c.ok){std::cerr<<c.name<<" UNEXPECTED rc="<<rc<<"\n"<<report;return 1;}assert(fs::exists(out/"aether.png")==c.ok);assert(!fs::exists(out/"aether.png.pending"));assert(report.find("chromium=NOT_RUN")!=std::string::npos);assert(report.find("https://")==std::string::npos);if(c.ok)assert(report.find("image_retained_bgra_bytes=4")!=std::string::npos);std::cout<<c.name<<"=PASS\n";}
std::cout<<"independent_cases="<<cases.size()<<" PASS\n";
}
