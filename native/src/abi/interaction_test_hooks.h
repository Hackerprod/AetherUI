#pragma once
#ifndef AETHER_INTERACTION_TESTING
#error Private interaction test hooks require the test-only build.
#endif
#include <aether/aether.h>
#include <array>
#include "../rendering/html_ltr/owned_budget.h"
namespace aether::interaction_test {
struct StorageStats {
 std::size_t current{},peak{},limit{};
 std::array<rendering::html_ltr::OwnedBudget::Usage,static_cast<std::size_t>(rendering::html_ltr::OwnedCategory::count)> categories{};
};
StorageStats storage(aether_interaction_t);
void storage_limit(aether_interaction_t,std::size_t);
void state_version(aether_interaction_t,std::uint64_t);
void sequence(aether_interaction_t,std::uint64_t);
}
