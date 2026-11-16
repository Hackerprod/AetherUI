#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <thread>
#include <cstdint>
struct node_identity_s;
struct aether_node_s;
namespace aether::style::css_scope {
enum class Scope { proven_disjoint, may_affect_scope, unknown };
enum class Reason { none, budget, selector_profile, syntax, conditional, snapshot, source_limit };
struct RuleEvidence { size_t begin=0,end=0,selector_begin=0,selector_end=0; Scope scope=Scope::unknown; Reason reason=Reason::none; };
struct SheetEvidence { std::vector<RuleEvidence> rules; // Boundary completeness is not semantic CSS coverage. UNKNOWN rules remain unsafe.
 bool boundary_complete=false; Reason reason=Reason::none; };
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
 // Selected constructor: full original DOM, no reparenting or filtered cascade.
 Proof(aether_node_s* root,aether_node_s* selected);
 SheetEvidence inspect(const std::string& sheet);
 std::string retained(const std::string& sheet, SourceMap* provenance=nullptr);
 size_t omitted=0, queries=0, remaining=16000000, rules_left=512;
private:
 std::vector<aether_node_s*> elements;
 size_t depth=0, siblings=0, data=0;
 bool usable=false,selected_mode=false;
 size_t rule_cap=512, css_remaining=128*1024;
 std::shared_ptr<node_identity_s> root_lifetime,target_lifetime;
 std::thread::id owner_thread;
 uint64_t epoch=0,structure_epoch=0,style_epoch=0,mutation_epoch=0;
 SheetEvidence evidence;
 Reason last_reason=Reason::none;
 bool fresh() const;
 Scope classify(const std::string& source);
 bool absent(const std::string& source);
};
}
