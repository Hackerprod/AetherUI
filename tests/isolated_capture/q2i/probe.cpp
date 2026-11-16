#include <cstdio>
#include <cstring>
extern "C" const char* aether_style_runtime_defaults_css();
#ifdef WEB_PROBE
namespace aether::style {const char* web_capture_defaults_css();}
#endif
int main(int argc,char**){const char*p=aether_style_runtime_defaults_css();
#ifdef WEB_PROBE
if(argc>1)p=aether::style::web_capture_defaults_css();
#endif
return fwrite(p,1,strlen(p)+1,stdout)==strlen(p)+1?0:1;}
