#include "codec.h"
#include "report.h"
#include <fstream>
#include <iostream>
#include <iterator>
using namespace aether::capture;
Bytes read(const char* p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("test input");return Bytes(std::istreambuf_iterator<char>(f),{});}
int main(int n,char** a){try{
 std::string op=a[1];
 if(op=="encode") {aether::rendering::Frame f;f.viewport.width=std::stoi(a[2]);f.viewport.height=std::stoi(a[3]);f.stride=std::stoull(a[4]);f.pixels=read(a[5]);auto b=deterministic_png(f);std::ofstream o(a[6],std::ios::binary);o.write((char*)b.data(),b.size());}
 else if(op=="validate"){auto b=read(a[2]);auto v=validate_png(b,std::stoul(a[3]),std::stoul(a[4]));std::cout<<v.sha256;}
 else if(op=="hash"){auto b=read(a[2]);std::cout<<sha256(b);}
 else if(op=="report"){Metrics m;m.status=Status::rendered_profile;m.error=Error::none;m.html.bytes=m.font.bytes=m.owners=1;m.width=m.height=1;std::cout<<serialize_metrics(m);}
 else if(op=="parse"){auto b=read(a[2]);auto m=parse_metrics(std::string_view((char*)b.data(),b.size()));std::cout<<serialize_metrics(m);}
 else return 3;
 return 0;
 }catch(const std::invalid_argument&){return 2;}catch(...){return 3;}}
