// Private Q operation includes fresh context creation, exactly one query and destruction.
#include "style_runtime_types.h"
#include "selector_match_context.h"
#include <aether/aether.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using Clock=std::chrono::steady_clock;
static void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static std::string read(const char* p){std::ifstream f(p,std::ios::binary);need(bool(f),"input");return {(std::istreambuf_iterator<char>(f)),{}};}
static void find(aether_node_t n,std::vector<aether_node_t>& out){if(aether_element_has_class(n,"target"))out.push_back(n);for(int i=0;i<aether_node_get_child_count(n);++i)find(aether_node_get_child(n,i),out);}
int main(int argc,char** argv){
 if(argc!=5)return 2; // html selector output expected-parent-links
 aether_doc_t doc=nullptr;
 try {
  const auto html=read(argv[1]),source=read(argv[2]);
  need(aether_doc_create(&doc)==AETHER_OK,"document");need(aether_doc_parse_html(doc,html.c_str())==AETHER_OK,"parse");
  std::vector<aether_node_t> targets;find(aether_doc_get_document_element(doc),targets);need(targets.size()==1,"one target");
  int links=0;for(auto n=aether_node_get_parent(targets[0]);n;n=aether_node_get_parent(n))++links;
  need(links==std::stoi(argv[4]),"fixed links");
  compiled_selector_s negative,positive;need(selector_parse(source.c_str(),negative),"negative compile");need(selector_parse("#present .target",positive),"positive compile");
  auto operation=[&](const compiled_selector_s& s){
#ifdef AETHER_ANCESTOR_FILTER_CANDIDATE
   selector_match_context_s context(true);
#else
   selector_match_context_s context;
#endif
   return selector_match(s,targets[0],context);
  };
  need(!operation(negative),"initial negative");need(operation(positive),"positive sanity");
  std::filesystem::create_directories(argv[3]);std::ofstream out(std::filesystem::path(argv[3])/"report.log");need(bool(out),"report");out<<std::setprecision(17);
  double sum=0;int false_count=0;
  for(int i=0;i<21;++i){
   auto start=Clock::now();const bool result=operation(negative);auto end=Clock::now();
   const double ms=std::chrono::duration<double,std::milli>(end-start).count();need(!result,"negative result");
   out<<"iteration="<<i<<",measured="<<(i>=5)<<",ms="<<ms<<",result="<<result<<'\n';
   if(i>=5){sum+=ms;false_count+=!result;}
  }
  need(false_count==16,"signature");out<<"private_match_operation_ms="<<sum/16<<"\nmeasured_count=16\nfalse_count="<<false_count<<"\nancestor_links="<<links<<"\nfailures=0\n";
  aether_doc_destroy(doc);return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';aether_doc_destroy(doc);return 1;}
}
