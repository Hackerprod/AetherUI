#pragma once
#include "../layout/document_text_context.h"
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace aether::rendering {
// Values only, not an authority: no DOM resolution or permission to emit scopes.
struct DocumentScrollRequestInput {std::string_view id;std::uint32_t x{},y{};};
struct DocumentScrollRequest {std::string id;std::uint32_t x{},y{};};
class DocumentScrollRequests final {
 std::vector<DocumentScrollRequest> entries_;
 std::vector<std::uint8_t> canonical_;
 DocumentScrollRequests()=default;
public:
 DocumentScrollRequests(const DocumentScrollRequests&)=delete;
 DocumentScrollRequests(DocumentScrollRequests&&)=default;
 DocumentScrollRequests& operator=(DocumentScrollRequests&&)=delete;
 // Caller owns stable input throughout this bounded synchronous call. No
 // callbacks. Result owns all bytes; failed construction publishes no result.
 static DocumentScrollRequests prepare(std::span<const DocumentScrollRequestInput>,aether::layout::DocumentTextBudget&);
 std::span<const DocumentScrollRequest> entries()const noexcept{return entries_;}
 // AETHSCRL ASCII domain, u32 version1/count; repeated u32 length, ID bytes,
 // u32 X,Y. All u32 little endian. Input order, no NUL/padding/trailing bytes.
 // Parent must regenerate its own expected bytes; not trust a worker digest.
 std::span<const std::uint8_t> canonical()const noexcept{return canonical_;}
 static constexpr std::size_t max_wire_bytes=16+65536+256*12;
};
}
