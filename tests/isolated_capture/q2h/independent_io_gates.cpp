#include "io.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace aether::capture;namespace fs=std::filesystem;
int checks=0;
template<class F>void rejects(F fn,IoCode expected){try{fn();assert(false);}catch(const IoError& e){assert(e.code()==expected);}++checks;}
void put(const fs::path&p){std::ofstream(p)<<"FOREIGN";}
mode_t mode(const fs::path&p){struct stat s{};assert(lstat(p.c_str(),&s)==0);return s.st_mode&07777;}
int main(int argc,char**argv){assert(argc==2);fs::path root=argv[1];fs::create_directory(root);auto fd=open_root(root.string());put(root/"data");
 assert(read_regular(fd.get(),"data",7).size()==7);++checks;
 rejects([&]{read_regular(fd.get(),"data",6);},IoCode::limit_exceeded);
 for(auto s:{"../data","/data",".data","a/b","a\\b","CON","COM1.txt",""})rejects([&]{read_regular(fd.get(),s,100);},IoCode::invalid_path);
 fs::create_symlink(root/"data",root/"link");fs::create_directory(root/"dir");assert(mkfifo((root/"fifo").c_str(),0600)==0);
 for(auto s:{"link","dir","fifo"})rejects([&]{read_regular(fd.get(),s,100);},IoCode::not_regular);
 auto dev=open_root("/dev");rejects([&]{read_regular(dev.get(),"null",100);},IoCode::not_regular);
 fs::create_directory_symlink(root,root/"rootlink");rejects([&]{open_root((root/"rootlink").string());},IoCode::io_error);
 for(mode_t mask:{mode_t(0022),mode_t(0777)}){auto previous=umask(mask);auto p=root/("mask"+std::to_string(mask));{
  auto out=OutputDirectory::create(p.string());assert(umask(mask)==mask);assert(mode(p)==0700);
  for(auto name:{".aether.png.pending",".worker.metrics.pending",".report.log.pending"})assert(mode(p/name)==0600);
  std::vector<std::uint8_t>b{0,1,2};write_all(out.png_fd(),b);assert(out.read_pending_png(3)==b);rejects([&]{out.read_pending_png(2);},IoCode::limit_exceeded);
  write_all(out.report_fd(),b);out.publish_png();out.publish_report();assert(mode(p/"aether.png")==0600&&mode(p/"report.log")==0600);assert(out.cleanup_metrics());assert(out.cleanup_owned());assert(!fs::exists(p));++checks;
 }put(p);rejects([&]{OutputDirectory::create(p.string());},IoCode::already_exists);assert(umask(mask)==mask);umask(previous);++checks;}
 // Existing final destinations are never overwritten, and foreign files survive cleanup.
 for(bool report:{false,true}){auto p=root/(report?"noreplaceReport":"noreplacePNG");auto out=OutputDirectory::create(p.string());auto dest=p/(report?"report.log":"aether.png");put(dest);rejects([&]{if(report)out.publish_report();else out.publish_png();},IoCode::already_exists);assert(!out.cleanup_owned());assert(fs::exists(dest));++checks;}
 // Detect swapped inode before check, sticky: restoring original cannot resume cleanup.
 {auto p=root/"swap";auto out=OutputDirectory::create(p.string());fs::rename(p/".aether.png.pending",p/"original");put(p/".aether.png.pending");rejects([&]{out.read_pending_png(100);},IoCode::changed);assert(!out.cleanup_owned());assert(fs::exists(p/".worker.metrics.pending"));fs::remove(p/".aether.png.pending");fs::rename(p/"original",p/".aether.png.pending");assert(!out.cleanup_owned());assert(fs::exists(p/".report.log.pending"));++checks;}
 {auto p=root/"directoryswap";auto out=OutputDirectory::create(p.string());fs::rename(p,root/"moved");fs::create_directory(p);put(p/"foreign");rejects([&]{out.publish_png();},IoCode::changed);assert(!out.cleanup_owned());assert(fs::exists(p/"foreign")&&fs::exists(root/"moved"/".aether.png.pending"));++checks;}
 {auto p=root/"modechange";auto out=OutputDirectory::create(p.string());assert(chmod((p/".aether.png.pending").c_str(),0644)==0);rejects([&]{out.publish_png();},IoCode::changed);assert(chmod((p/".aether.png.pending").c_str(),0600)==0);assert(!out.cleanup_owned());assert(fs::exists(p/".worker.metrics.pending"));++checks;}
 {auto p=root/"foreignentry";auto out=OutputDirectory::create(p.string());put(p/"foreign");assert(!out.cleanup_owned());assert(fs::exists(p/"foreign"));++checks;}
 {auto p=root/"failureReport";auto out=OutputDirectory::create(p.string());assert(out.cleanup_pixels_metrics());std::vector<std::uint8_t>b{'F','A','I','L'};write_all(out.report_fd(),b);out.publish_report();assert(fs::exists(p/"report.log")&&!fs::exists(p/"aether.png"));assert(out.cleanup_owned());++checks;}
 // Controlled output shortens old bytes; read never leaks stale suffix.
 {auto p=root/"shorten";auto out=OutputDirectory::create(p.string());std::vector<std::uint8_t>a(100,42),b{9};write_all(out.png_fd(),a);write_all(out.png_fd(),b);assert(out.read_pending_png(1)==b);assert(out.cleanup_owned());++checks;}
 std::cout<<"Q2h independent FD checks="<<checks<<" PASS; stable-path and deterministic-before-check replacements only; no hostile concurrent-race guarantee\n";
}
