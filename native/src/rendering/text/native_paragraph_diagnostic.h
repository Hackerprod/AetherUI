#pragma once
// Frozen source diagnostics only: no CSS normalization, paragraph grouping,
// shaping, font selection, geometry, provider, DOM handle or render authority.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aether::rendering::text {
struct ParagraphDiagnosticEpochs {
    // Historical labels sampled by the owner transaction. Equality of these
    // labels does NOT establish liveness or authorize an operational consumer.
    std::uint64_t document_identity{}, generation{}, input_epoch{},
        structure_epoch{}, style_tree_epoch{}, sheet_epoch{};
};
struct ParagraphDiagnosticLimits {
    std::size_t nodes=65536, source_bytes=1048576, ranges=65536, requests=65536;
};
struct ParagraphDiagnosticBudget {
    ParagraphDiagnosticLimits limits{};
    std::size_t nodes{}, source_bytes{}, ranges{}, requests{};
};
struct ParagraphDiagnosticPiece {
    std::uint64_t node_identity{};
    std::string_view bytes;
};
struct ParagraphDiagnosticRange {
    std::uint64_t node_identity{};
    std::size_t piece{}, source_begin{}, source_length{}, frozen_begin{};
};
enum class ParagraphDiagnosticStatus { prepared, resource_limit, cancelled };
class FrozenParagraphDiagnostic final {
public:
    const ParagraphDiagnosticEpochs epochs;
    // These are raw bytes, NOT CSS-normalized text or a renderable paragraph.
    const std::string source_bytes;
    const std::vector<ParagraphDiagnosticRange> provenance;
    const bool context_unproven=true;
    const bool font_unresolved=true;
    FrozenParagraphDiagnostic(ParagraphDiagnosticEpochs e, std::string bytes,
                              std::vector<ParagraphDiagnosticRange> ranges)
      : epochs(e), source_bytes(std::move(bytes)), provenance(std::move(ranges)) {}
};
using ParagraphDiagnosticRef=std::shared_ptr<const FrozenParagraphDiagnostic>;
inline bool paragraph_diagnostic_charge(std::size_t& used, std::size_t amount,
                                        std::size_t limit) noexcept {
    if (used>limit || amount>limit-used) return false;
    used+=amount;
    return true;
}
// The owner uses one budget for the whole document transaction, never a fresh
// budget per node/request. Inputs count conservatively as one node per piece,
// including empty/repeated pieces. One compact range per piece avoids byte-wise
// provenance expansion. No allocation occurs before all charges are checked.
// Allocation failure propagates; out and budget remain unchanged. The owner
// must still reject stale epochs/cancellation before publishing the revision.
inline ParagraphDiagnosticStatus freeze_paragraph_diagnostic(
    const std::vector<ParagraphDiagnosticPiece>& pieces,
    ParagraphDiagnosticEpochs epochs, ParagraphDiagnosticBudget& budget,
    ParagraphDiagnosticRef& out, bool cancelled=false) {
    if (cancelled) return ParagraphDiagnosticStatus::cancelled;
    auto staged_budget=budget;
    if (!paragraph_diagnostic_charge(staged_budget.requests,1,budget.limits.requests) ||
        !paragraph_diagnostic_charge(staged_budget.nodes,pieces.size(),budget.limits.nodes) ||
        !paragraph_diagnostic_charge(staged_budget.ranges,pieces.size(),budget.limits.ranges))
        return ParagraphDiagnosticStatus::resource_limit;
    const auto before=staged_budget.source_bytes;
    for (const auto& piece:pieces)
        if (!paragraph_diagnostic_charge(staged_budget.source_bytes,piece.bytes.size(),budget.limits.source_bytes))
            return ParagraphDiagnosticStatus::resource_limit;
    std::string bytes;
    std::vector<ParagraphDiagnosticRange> ranges;
    bytes.reserve(staged_budget.source_bytes-before);
    ranges.reserve(pieces.size());
    for (std::size_t i=0;i<pieces.size();++i) {
        const auto& piece=pieces[i];
        ranges.push_back({piece.node_identity,i,0,piece.bytes.size(),bytes.size()});
        if (!piece.bytes.empty()) bytes.append(piece.bytes.data(),piece.bytes.size());
    }
    auto staged=std::make_shared<const FrozenParagraphDiagnostic>(epochs,std::move(bytes),std::move(ranges));
    out=std::move(staged);
    budget=staged_budget;
    return ParagraphDiagnosticStatus::prepared;
}
}
