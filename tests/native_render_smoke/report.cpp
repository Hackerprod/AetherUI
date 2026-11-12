#include "report.h"
#include "fixture.h"
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>
namespace aether::smoke {
namespace {
const char* name(Status s) {switch(s){case Status::pass:return "PASS";case Status::skip:return "SKIP";case Status::fail:return "FAIL";}return "INVALID";}
std::string escape(std::string_view in) {
 std::string s;for(unsigned char c:in) {if(c=='\\')s+="\\\\";else if(c=='\n')s+="\\n";
 else if(c=='\r')s+="\\r";else if(c=='=')s+="\\=";
 else if(c<32||c==127) {constexpr char h[]="0123456789ABCDEF";s+="\\x";s+=h[c>>4];s+=h[c&15];}
 else s+=static_cast<char>(c);}return s;
}
constexpr const char* keys[]={"build","geometry.render","geometry.png","geometry.verify",
 "text.layout","text.draw","text.png","text.verify"};
}
bool valid_run_id(std::string_view s) noexcept {
 if(s.empty()||s.size()>64)return false;
 for(char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
 return true;
}
bool coherent(const Report& r) noexcept {
 if(!valid_run_id(r.run_id))return false;
 if(r.provenance!=Provenance::measured&&r.provenance!=Provenance::not_measured&&r.provenance!=Provenance::synthetic_test)return false;
 for(std::size_t i=0;i<r.phases.size();++i) {
  const auto& p=r.phases[i];
  if(p.status!=Status::pass&&p.status!=Status::skip&&p.status!=Status::fail)return false;
  if((p.status==Status::skip||i==0)&&p.duration_ns)return false;
  if(p.duration_ns&&*p.duration_ns<0)return false;
  if(p.status!=Status::pass&&p.reason.empty())return false;
  if(i>0&&p.status==Status::pass&&!p.duration_ns&&p.reason.empty())return false;
  // Each fixture's phases form a chain, both rooted in build.
  const auto dependency=(i==1||i==4)?0:i-1;
  if(i>0&&p.status!=Status::skip&&r.phases[dependency].status!=Status::pass)return false;
 }
 for(auto n:{r.text_width,r.text_height,r.text_baseline})if(n&&!std::isfinite(*n))return false;
 if((r.text_width&&*r.text_width<0)||(r.text_height&&*r.text_height<0))return false;
 if(r.provenance==Provenance::not_measured) {
  for(const auto& phase:r.phases)if(phase.duration_ns)return false;
  if(r.text_width||r.text_height||r.text_baseline)return false;
 }
 if(r.phases[4].status==Status::pass&&(!r.text_width||!r.text_height||!r.text_baseline))return false;
 if(r.phases[4].status!=Status::pass&&(r.text_width||r.text_height||r.text_baseline))return false;
 return true;
}
Status overall(const Report& r) noexcept {
 if(!coherent(r))return Status::fail;
 bool skipped=false;
 for(const auto& p:r.phases){if(p.status==Status::fail)return Status::fail;if(p.status==Status::skip)skipped=true;}
 return skipped?Status::skip:Status::pass;
}
bool accepts_real_smoke(const Report& r) noexcept {return overall(r)==Status::pass&&r.provenance==Provenance::measured;}
std::string serialize(const Report& r) {
 std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(17);
 o<<"report.version=1\nrun.id="<<escape(r.run_id)<<"\nrun.status="<<name(overall(r))
  <<"\nacceptance.scope=pipeline_sanity_only\nacceptance.real_smoke="<<(accepts_real_smoke(r)?"PASS":"NOT_ACCEPTED")
  <<"\nfixture.geometry.id="<<geometry_id<<"\nfixture.text.id="<<text_id
  <<"\nmetrics.provenance="<<(r.provenance==Provenance::measured?"measured":r.provenance==Provenance::synthetic_test?"synthetic_test":"not_measured")
  <<"\nmetrics.cpu=not_collected\nmetrics.rss=not_collected\nfont.requested=sans-serif\nfont.resolved=not_verified"
  <<"\ncairo.version="<<escape(r.cairo_version)<<"\npango.version="<<escape(r.pango_version)
  <<"\nsource.revision=unknown\ncompiler.identity=unknown\nartifact.geometry=geometry.png\nartifact.text=text.png\nreport.finalization=final_report_log_only\n";
 for(std::size_t i=0;i<r.phases.size();++i){const auto& p=r.phases[i];o<<keys[i]<<".status="<<name(p.status)<<'\n';
  if(!p.reason.empty())o<<keys[i]<<".reason="<<escape(p.reason)<<'\n';
  o<<keys[i]<<".duration_ns=";if(p.duration_ns)o<<*p.duration_ns;else o<<"not_measured";o<<'\n';}
 for(auto [key,n]:{std::pair{"text.width_dip",r.text_width},std::pair{"text.height_dip",r.text_height},std::pair{"text.baseline_dip",r.text_baseline}}) {
  o<<key<<'=';if(n)o<<*n;else o<<"not_measured";o<<'\n';}
 return o.str();
}
}
