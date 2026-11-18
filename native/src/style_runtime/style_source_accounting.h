#pragma once
#include <cstddef>
#include <cstdint>
#include <array>
#include <cmath>

// Private, bounded diagnostic accounting. Never a typography/font authority.
// Counts refer to compiler input, not declarations winning the cascade.
enum class style_source_loss : uint32_t {
    unknown_declaration = 1u << 0, rejected_declaration = 1u << 1,
    malformed_declaration = 1u << 2, selector = 1u << 3,
    at_rule = 1u << 4, conditional_source = 1u << 5,
    external_source = 1u << 6, incomplete_parse = 1u << 7,
    budget = 1u << 8
};
// Loss attribution is private evidence, never permission to render. Default
// construction is unproven (including synthetic/test accounting).
struct style_source_loss_attribution_s {
    bool complete = false;
    uint32_t unattributed = 0;
    size_t direct_width_rejections = 0;
    bool operator==(const style_source_loss_attribution_s&) const = default;
};
struct style_source_accounting_s {
    static constexpr size_t max_source_bytes = 16u * 1024u * 1024u;
    static constexpr size_t max_declarations = 65536;
    static constexpr size_t max_rules = 65536;
    static constexpr size_t max_compiled_bytes = 32u*1024u*1024u;
    size_t source_bytes = 0, declarations = 0, rules = 0;
    size_t compiled_rules=0, expanded_declarations=0, compiled_bytes=0;
    uint32_t losses = 0;
    bool inputs_accounted = true;
    bool external_pending = false;
    // Capability evidence from the actual compiler, before expansion/discard.
    // Not a loss exemption and never permission to render by itself.
    bool b0_subset = true;
    bool b3_subset = true;
    // Rejection-only compiler provenance. Fixed storage, no unbudgeted allocation.
    // Index spells the exact property/token pair: min-width:none,
    // min-height:none, max-width:auto, max-height:auto, invalid width%, height%.
    std::array<size_t,6> b3_rejected_values{};
    size_t b3_first_rejection_declaration = 0;
    unsigned b3_first_rejection_kind = 6;
    double b3_first_rejection_number = 0;
    bool b3_rejection_integrity = true;
    bool b3_has_value_rejection() const noexcept {
        size_t total=0;
        for(auto n:b3_rejected_values){if(n>declarations-total)return false;total+=n;}
        const bool token_valid=std::isfinite(b3_first_rejection_number) &&
            (b3_first_rejection_kind<4?b3_first_rejection_number==0:
             b3_first_rejection_kind==4?(b3_first_rejection_number<0||b3_first_rejection_number>100):b3_first_rejection_kind==5);
        return token_valid && b3_rejection_integrity && total>0 && b3_first_rejection_kind<6 &&
            b3_rejected_values[b3_first_rejection_kind]>0 &&
            b3_first_rejection_declaration<declarations;
    }
    void b3_reject_value(unsigned kind,double number=0) noexcept {
        if(kind>=6 || !declarations || declarations>max_declarations) {b3_rejection_integrity=false;return;}
        if(b3_rejected_values[kind]>=max_declarations){b3_rejection_integrity=false;return;}
        ++b3_rejected_values[kind];
        if(b3_first_rejection_kind==6){b3_first_rejection_kind=kind;b3_first_rejection_declaration=declarations-1;b3_first_rejection_number=number;}
    }
    // No diagnostic is currently classified as harmless for B0.
    bool b0_diagnostics_clean = true;
    style_source_loss_attribution_s attribution;
    static style_source_accounting_s tracked() noexcept {
        style_source_accounting_s result; result.attribution.complete=true; return result;
    }
    bool attribution_consistent() const noexcept {
        const uint32_t described=attribution.unattributed | (attribution.direct_width_rejections
            ? static_cast<uint32_t>(style_source_loss::rejected_declaration) : 0u);
        return losses==described && attribution.direct_width_rejections<=max_declarations;
    }
    bool visual_losses_certified() const noexcept {
        return attribution.complete && attribution_consistent() && !attribution.unattributed;
    }
    void loss(style_source_loss reason) noexcept {
        const auto bits=static_cast<uint32_t>(reason);
        losses |= bits; attribution.unattributed |= bits; attribution.complete=false;
    }
    void reject_direct_width() noexcept {
        // Do not repair a partial certificate or an untracked write to losses.
        attribution.complete=attribution.complete && attribution_consistent();
        losses |= static_cast<uint32_t>(style_source_loss::rejected_declaration);
        if (attribution.direct_width_rejections==max_declarations) {
            incomplete(style_source_loss::budget); return;
        }
        ++attribution.direct_width_rejections;
    }
    void incomplete(style_source_loss reason) noexcept { loss(reason); inputs_accounted = false; }
    void add(size_t& value, size_t increment, size_t limit) noexcept {
        if (value > limit || increment > limit - value) { value = limit; incomplete(style_source_loss::budget); }
        else value += increment;
    }
    void merge(const style_source_accounting_s& other) noexcept {
        b3_rejection_integrity = b3_rejection_integrity && other.b3_rejection_integrity;
        if(other.b3_first_rejection_kind<6 && b3_first_rejection_kind==6){
            b3_first_rejection_kind=other.b3_first_rejection_kind;b3_first_rejection_number=other.b3_first_rejection_number;
            if(other.b3_first_rejection_declaration>max_declarations-declarations)b3_rejection_integrity=false;
            else b3_first_rejection_declaration=declarations+other.b3_first_rejection_declaration;
        }
        for(size_t i=0;i<6;++i){if(other.b3_rejected_values[i]>max_declarations-b3_rejected_values[i])b3_rejection_integrity=false;
            else b3_rejected_values[i]+=other.b3_rejected_values[i];}
        b3_subset = b3_subset && other.b3_subset;
        attribution.complete=attribution.complete && other.attribution.complete &&
            attribution_consistent() && other.attribution_consistent();
        attribution.unattributed |= other.attribution.unattributed;
        if (other.attribution.direct_width_rejections>max_declarations ||
            attribution.direct_width_rejections>max_declarations-other.attribution.direct_width_rejections) {
            attribution.direct_width_rejections=max_declarations;
            incomplete(style_source_loss::budget);
        } else attribution.direct_width_rejections+=other.attribution.direct_width_rejections;
        add(source_bytes, other.source_bytes, max_source_bytes);
        add(declarations, other.declarations, max_declarations);
        add(rules, other.rules, max_rules);
        add(compiled_rules,other.compiled_rules,max_rules);
        add(expanded_declarations,other.expanded_declarations,max_declarations);
        add(compiled_bytes,other.compiled_bytes,max_compiled_bytes);
        losses |= other.losses;
        inputs_accounted = inputs_accounted && other.inputs_accounted;
        external_pending = external_pending || other.external_pending;
        b0_subset = b0_subset && other.b0_subset;
        b0_diagnostics_clean = b0_diagnostics_clean && other.b0_diagnostics_clean;
    }
};

// Operational input budget, shared across document sheets and inline inputs.
// A compiler call stages this value and commits it only on successful publication.
struct style_source_budget_s {
    size_t source_bytes = 0, declarations = 0, rules = 0;
    size_t compiled_rules=0, expanded_declarations=0, compiled_bytes=0;
    size_t source_bytes_limit = style_source_accounting_s::max_source_bytes;
    size_t declarations_limit = style_source_accounting_s::max_declarations;
    size_t rules_limit = style_source_accounting_s::max_rules;
    size_t compiled_rules_limit=style_source_accounting_s::max_rules;
    size_t expanded_declarations_limit=style_source_accounting_s::max_declarations;
    size_t compiled_bytes_limit=style_source_accounting_s::max_compiled_bytes;
};
// Observer of actual admitted work, intentionally retained even when a call fails.
struct style_source_work_s {
    size_t work_source_bytes_copied = 0;
    size_t work_declaration_spans = 0;
    size_t work_rule_starts = 0;
    size_t work_expanded_declaration_copies=0;
};

inline bool style_source_expansion_product(size_t a,size_t b,size_t& result) noexcept {
    if(b && a>static_cast<size_t>(-1)/b)return false;
    result=a*b;return true;
}
