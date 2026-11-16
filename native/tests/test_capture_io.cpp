#include "../tools/aether_capture_real/io.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
using namespace aether::capture;
namespace fs=std::filesystem;
template<class F> void rejects(F fn){bool caught=false;try{fn();}catch(const IoError&){caught=true;}assert(caught);}
int main(){char pattern[]="/tmp/q2h-io-test-XXXXXX";auto* base=mkdtemp(pattern);assert(base);const std::string root=base;fs::create_directory(root+"/input");std::ofstream(root+"/input/a.txt")<<"abc";auto fd=open_root(root+"/input");assert(read_regular(fd.get(),"a.txt",3).size()==3);rejects([&]{read_regular(fd.get(),"a.txt",2);});for(auto s:{"../a.txt","/a.txt",".","..",".hidden","CON","con.txt","NUL.jpg","lpt1.x","COM9","a/b"})rejects([&]{read_regular(fd.get(),s,20);});for(auto s:{root+"//input",root+"/./input",root+"/input/../input",root+"/input/"})rejects([&]{open_root(s);});assert(open_root("/").get()>=0);symlink("input",(root+"/linkdir").c_str());rejects([&]{open_root(root+"/linkdir");});symlink("a.txt",(root+"/input/link").c_str());rejects([&]{read_regular(fd.get(),"link",20);});assert(mkfifo((root+"/input/fifo").c_str(),0600)==0);rejects([&]{read_regular(fd.get(),"fifo",20);});fs::create_directory(root+"/input/dir");rejects([&]{read_regular(fd.get(),"dir",20);});
 {auto dev=open_root("/dev");rejects([&]{read_regular(dev.get(),"null",20);});}
 {auto previous=umask(0777);rejects([&]{OutputDirectory::create(root+"/input");});assert(umask(0777)==0777);umask(previous);}
 {auto previous=umask(0022);auto normal=OutputDirectory::create(root+"/normalmask");assert(umask(0022)==0022);umask(previous);assert(normal.cleanup_owned());}
 const std::vector<std::uint8_t> png{1,2,3},metrics{4,5},report{6};
 {auto previous=umask(0777);auto out=OutputDirectory::create(root+"/out");assert(umask(0777)==0777);umask(previous);struct stat st{};assert(stat((root+"/out").c_str(),&st)==0&&(st.st_mode&0777)==0700);assert(fstat(out.png_fd(),&st)==0&&(st.st_mode&0777)==0600);write_all(out.png_fd(),png);write_all(out.metrics_fd(),metrics);write_all(out.report_fd(),report);rejects([&]{out.read_pending_png(2);});assert(out.read_pending_png(3)==png&&out.read_pending_metrics(2)==metrics);out.publish_png();assert(fs::exists(root+"/out/aether.png")&&!fs::exists(root+"/out/report.log"));out.publish_report();assert(out.cleanup_metrics());assert(fs::exists(root+"/out/aether.png")&&fs::exists(root+"/out/report.log"));rejects([&]{OutputDirectory::create(root+"/out");});assert(out.cleanup_owned());assert(!fs::exists(root+"/out"));}
 {auto out=OutputDirectory::create(root+"/failure");write_all(out.png_fd(),png);out.publish_png();assert(out.cleanup_pixels_metrics());write_all(out.report_fd(),report);out.publish_report();assert(!fs::exists(root+"/failure/aether.png")&&fs::exists(root+"/failure/report.log"));assert(out.cleanup_owned());}
 {auto out=OutputDirectory::create(root+"/swap");fs::rename(root+"/swap/.aether.png.pending",root+"/swap/owned-detached");std::ofstream(root+"/swap/.aether.png.pending")<<"foreign";rejects([&]{out.read_pending_png(10);});assert(!out.cleanup_owned());assert(fs::exists(root+"/swap/.worker.metrics.pending")&&fs::file_size(root+"/swap/.aether.png.pending")==7);}
 {auto out=OutputDirectory::create(root+"/swapdir");fs::rename(root+"/swapdir",root+"/detached");fs::create_directory(root+"/swapdir");std::ofstream(root+"/swapdir/foreign")<<"safe";rejects([&]{out.publish_png();});assert(!out.cleanup_owned()&&fs::exists(root+"/swapdir/foreign")&&fs::exists(root+"/detached/.aether.png.pending"));}
 {auto out=OutputDirectory::create(root+"/existing");std::ofstream(root+"/existing/aether.png")<<"foreign";rejects([&]{out.publish_png();});assert(fs::file_size(root+"/existing/aether.png")==7);assert(!out.cleanup_owned());assert(fs::file_size(root+"/existing/aether.png")==7);}
 // Test-only cleanup owns this private fixture tree, never used by production IO.
 fs::remove_all(root);std::cout<<"FD proposal test PASS: bounded input, symlinks/FIFO, permissions, publication, failure report, replacement preservation\n";
}
