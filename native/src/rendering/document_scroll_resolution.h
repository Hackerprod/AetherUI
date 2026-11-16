#pragma once
#include "document_scroll_requests.h"
#include "../css/style_map_types.h"
#include <map>
namespace aether::rendering {
enum class DocumentScrollResolutionError {stale,missing,ambiguous};
// Inactive DOM evidence only. Does not authorize a primary box or scrolling.
class DocumentScrollResolution final {
 friend class DocumentViewportAuthority;
 friend class aether::layout::DocumentTextTestAccess;
 struct Node {const aether_node_s* key{};const aether_node_s* parent{};std::shared_ptr<node_identity_s> identity;std::vector<aether_node_s*> children;std::string id;bool has_id{};aether_node_type_t type{};};
public:
 struct Target {std::size_t request_index{};std::shared_ptr<node_identity_s> identity;std::uint32_t x{},y{};};
private:
 std::thread::id thread_;
 aether_doc_s* doc_{};aether_style_map_s* map_{};aether::style::MapVersionRef version_;
 std::shared_ptr<node_identity_s> document_;
 std::uint64_t structure_{},mutation_{},style_{},nonstate_{},validation_work_{};
 aether::layout::DocumentTextBudget* budget_{};
 DocumentScrollRequests requests_;
 std::vector<Node> nodes_;
 std::map<const aether_node_s*,Target> targets_;
 // Initial DOM must be valid, live and exclusively used by owner_thread. Its
 // caller retains document/map/budget through this evidence's lifetime. Tokens
 // detect retirement; they do not own DOM memory. No callbacks during capture.
 DocumentScrollResolution(aether_doc_s*,aether_style_map_s*,const DocumentScrollRequests&,
  aether::layout::DocumentTextBudget&,std::thread::id owner_thread,bool storage_precharged=false);
 static std::unique_ptr<DocumentScrollResolution> capture(aether_doc_s*,aether_style_map_s*,const DocumentScrollRequests&,aether::layout::DocumentTextBudget&,std::thread::id);
 static DocumentScrollRequests own(const DocumentScrollRequests&,aether::layout::DocumentTextBudget&);
 bool stamp()const noexcept;
public:
 DocumentScrollResolution(const DocumentScrollResolution&)=delete;
 // Charged full comparison; may throw budget exhaustion, never follows a new
 // child pointer. Call after callback before binding targets to layout evidence.
 bool accepts()const;
 const Target* find(const aether_node_s* key)const noexcept {auto i=targets_.find(key);return i==targets_.end()?nullptr:&i->second;}
 const DocumentScrollRequests& requests()const noexcept{return requests_;}
 std::size_t size()const noexcept{return targets_.size();}
};
}
