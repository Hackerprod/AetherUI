#include "aether_visual_compare/cli.hpp"
#include <csignal>
#include <iostream>
#include <random>
namespace {
volatile std::sig_atomic_t interrupted=0;
void interrupt_handler(int) {interrupted=1;}
std::string new_run_id() {
 std::random_device random;std::uniform_int_distribution<unsigned> nibble(0,15);
 constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(32);
 for(unsigned i=0;i<32;++i)result+=hex[nibble(random)];
 return result;
}
}
int main(int argc,char** argv) {
 try {
  std::vector<std::string_view> args;for(int i=1;i<argc;++i)args.emplace_back(argv[i]);
  std::signal(SIGINT,interrupt_handler);
  auto filesystem=avc::native_filesystem();
  return avc::run_cli(args,*filesystem,{[]{return interrupted!=0;}},new_run_id,std::cout,std::cerr);
 }catch(...){std::cerr<<"INTERNAL_FAILURE\n";return 14;}
}
