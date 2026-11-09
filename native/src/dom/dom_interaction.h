#pragma once
#include <aether/aether.h>
#include <map>
#include <memory>
#include <string>
#include <vector>
struct DomOwnerControl;
struct DomInteractionRegistry;
struct DomInteractionClaim;
struct DomInteractionStage;
using DomInteractionStageRef=std::shared_ptr<DomInteractionStage>;
/* Retain the registry through every edit's validation/publication, including
 * edits with no claims yet. New registration is covered by interaction_serial. */
aether_status_t aether_dom_interaction_prepare_stage(
    const std::shared_ptr<DomOwnerControl>& owner,
    const std::vector<aether_node_s*>& disconnect, aether_node_s* target,
    const std::map<std::string,std::string,std::less<>>* attributes,
    DomInteractionStageRef& out, bool retiring=false) noexcept;
bool aether_dom_interaction_validate_stage(const DomInteractionStageRef& stage) noexcept;
void aether_dom_interaction_commit_stage(const DomInteractionStageRef& stage) noexcept;
/* Covers direct retirement/destroy. Records/routes are preallocated in claims. */
void aether_dom_interaction_retire_node(aether_node_s* node,bool exhausted) noexcept;
