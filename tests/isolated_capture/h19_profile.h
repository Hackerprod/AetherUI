#pragma once
#include <aether/aether.h>
#include <memory>
#include <string_view>
namespace h19 {
// Per-capture proof only: never reused after DOM/style changes.
class Gate {
 struct Impl;std::unique_ptr<Impl> impl;
public:
 Gate();~Gate();Gate(const Gate&)=delete;
 bool add(std::string_view selector);
 bool check(aether_node_t root,aether_style_map_t map);
 bool needed() const;
};
}
