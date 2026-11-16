#pragma once
#include "real_html_normalization.h"
#include <map>
struct aether_doc_s;
struct aether_node_s;
namespace aether::rendering::text::truetype {
enum class HtmlAdmissionError { invalid_input, unsupported, limit_exceeded, cancelled, out_of_memory };
struct HtmlAdmissionNode {
 enum class Kind { element, text, comment };
 Kind kind{Kind::element};
 std::string tag, text;
 std::map<std::string,std::string,std::less<>> attributes, declarations;
 std::vector<std::size_t> children;
};
// Operation-local by-value source proof, not an alternate rendering DOM.
// Closed profile: explicit html/body scaffold with zero individual margins,
// padding and border widths; block div descendants; one aether-text owner.
// Raw input <=64KiB, <=128 nodes, <=16 element levels, <=128 declarations.
// Both raw and decoded style totals <=4KiB; decoded text <=4096 bytes.
// No source recovery, attribute/declaration/ID duplicates, external text or
// unsupported CSS. Only quoted id/style attributes and six named entities.
// CR source is rejected rather than relying on parser preprocessing.
// Comments preserve literal bytes; owner comments contribute empty pieces.
struct HtmlAdmission {
 HtmlWhiteSpaceProfile profile{HtmlWhiteSpaceProfile::pre};
 std::vector<HtmlAdmissionNode> nodes;
 std::vector<std::size_t> roots;
 std::size_t owner{};
 std::vector<std::string> pieces;
 std::string decoded_text;
};
using AdmissionResult=std::variant<HtmlAdmission,HtmlAdmissionError>;
AdmissionResult admit_real_html(std::string_view,std::int32_t font_size26_6,LineBox,Cancellation* =nullptr) noexcept;
// Caller must own a live operation-local DOM and check thread/epoch before call.
// Returned pointer never leaves that operation. Exact structure/attrs/text match.
aether_node_s* verify_real_html_dom(const HtmlAdmission&,aether_doc_s*) noexcept;
}
