#pragma once
// R0c experimental evidence. No replay consumer or reuse eligibility exists.
#include "style_source_accounting.h"
#include "../dom/dom_types.h"
#include <optional>
#include <memory>
#include <string>
#include <vector>
namespace aether::style {
struct CapturedDiagnostic {
    std::optional<std::string> code, property, value, message;
};
using CapturedDiagnostics = std::vector<CapturedDiagnostic>;
// These families are deliberately UNKNOWN, including built-in registrations.
// A mask denotes possible opaque effects, NOT a count or observed invocation.
enum CapturedUnknownCallback : uint32_t {
    parse_unknown=1, parse_failure_unknown=2, css_wide_unknown=4,
    inherit_explicit_unknown=8, inherit_context_unknown=16,
    shorthand_unknown=32, apply_unknown=64, apply_checked_unknown=128,
    apply_contextual_unknown=256, apply_contextual_checked_unknown=512,
    value_audit_unknown=1024, all_callback_families_unknown=2047
};
enum class CapturedEffectKind { compute_attempt, sheet_aggregate, link, style, text_limited };
struct CapturedEffect {
    CapturedEffectKind kind=CapturedEffectKind::compute_attempt;
    std::shared_ptr<node_identity_s> host; // never a synthetic node pointer
    aether_pseudo_element_t pseudo{};
    size_t sequence=0, diagnostic_begin=0, diagnostic_end=0;
    bool completed=false, inline_present=false, diagnostics_captured=false;
    bool generated_content_present=false; // false pseudo may be discarded after compute
    uint32_t unknown_callbacks=all_callback_families_unknown;
    style_source_accounting_s accounting_before, accounting_after, inline_local;
    style_source_budget_s budget_before, budget_after, inline_budget_before, inline_budget_after;
};
struct CapturedEffects {
    uint64_t source_generation=0, published_generation=0;
    uint64_t input_epoch=0, sheet_epoch=0, structure_epoch=0, style_tree_epoch=0;
    std::shared_ptr<node_identity_s> document, root;
    std::vector<CapturedEffect> events;
    std::shared_ptr<const CapturedDiagnostics> diagnostics;
    bool capture_finished=false;
    // Unknown callback behavior prevents any effects-complete certificate.
    const bool effects_complete=false;
};
using CapturedEffectsRef=std::shared_ptr<const CapturedEffects>;
}
