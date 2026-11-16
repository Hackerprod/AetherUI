#pragma once
#include "real_html_admission.h"
namespace aether::rendering::text::truetype {
struct HtmlBlockOwner {
 std::string id;
 std::size_t node_index{};
 HtmlWhiteSpaceProfile profile{HtmlWhiteSpaceProfile::pre};
 std::vector<std::string> pieces;
 std::string decoded_text;
};
// Closed multi-block source proof. Owners are in source/DOM order, never ID order.
// No DOM pointers are retained. Q2d single-owner entry is unchanged.
// Raw 64 KiB, 128 nodes, 16 element levels, 512 declarations; raw and
// decoded styles each 16 KiB. Decoded text is 4096 bytes across all owners;
// at most 128 pieces including zero-byte owner comments. IDs are copied.
// logical_bytes accounts logical record/value/string bytes before insertion
// into the proof, including retained copies; excludes capacity, allocator
// overhead and bounded scanner scratch. No claim about process RSS.
// steps covers explicit admission loops; bounded standard-library work is
// synchronous and not an instruction/CPU-time counter.
struct HtmlBlocksAdmission {
 std::vector<HtmlAdmissionNode> nodes;
 std::vector<std::size_t> roots;
 std::vector<HtmlBlockOwner> owners;
 std::size_t steps{},logical_bytes{};
};
using BlocksAdmissionResult=std::variant<HtmlBlocksAdmission,HtmlAdmissionError>;
BlocksAdmissionResult admit_real_html_blocks(std::string_view,const std::vector<std::string>&,std::int32_t,LineBox,Cancellation* =nullptr) noexcept;
// Mismatch returns empty; allocation failures propagate for typed wrapper handling.
std::vector<aether_node_s*> verify_real_html_blocks_dom(const HtmlBlocksAdmission&,aether_doc_s*);
std::vector<aether_node_s*> verify_real_html_blocks_nodes(const HtmlBlocksAdmission&,aether_doc_s*);
}
