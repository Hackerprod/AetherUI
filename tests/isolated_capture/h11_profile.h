#pragma once
#include <cstddef>
#include <string>
#include <vector>
struct aether_node_s;
namespace h11 {
// Per-capture, never cached across DOM mutations. Limits bound proof work, not
// engine CSS support. Failure to prove absence retains the old H3 restriction.
struct Proof {
 explicit Proof(aether_node_s* root);
 std::string retained(const std::string& sheet);
 size_t omitted=0, queries=0, remaining=16000000, rules_left=512;
private:
 std::vector<aether_node_s*> elements;
 size_t depth=0, siblings=0, data=0;
 bool usable=false;
 bool absent(const std::string& source);
};
}
