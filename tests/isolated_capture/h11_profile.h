#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include <array>
struct aether_node_s;
namespace h11 {
// Per-capture, never cached across DOM mutations. Limits bound proof work, not
// engine CSS support. Failure to prove absence retains the old H3 restriction.
struct SourceMap {
 struct Range {size_t output,original,length;};
 std::array<Range,512> ranges{};size_t count=0;bool identity=true;
 bool locate(size_t a,size_t z,size_t& oa,size_t& oz) const {
  if(identity){oa=a;oz=z;return true;}
  for(size_t i=0;i<count;++i){auto r=ranges[i];if(a>=r.output&&z>=a&&z-r.output<=r.length){oa=r.original+a-r.output;oz=r.original+z-r.output;return true;}}return false;
 }
};
struct Proof {
 explicit Proof(aether_node_s* root);
 std::string retained(const std::string& sheet, SourceMap* provenance=nullptr);
 size_t omitted=0, queries=0, remaining=16000000, rules_left=512;
private:
 std::vector<aether_node_s*> elements;
 size_t depth=0, siblings=0, data=0;
 bool usable=false;
 bool absent(const std::string& source);
};
}
