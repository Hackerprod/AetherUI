#pragma once
#include <cstddef>
#include <cstdint>

// Private, bounded diagnostic accounting. Never a typography/font authority.
// Counts refer to compiler input, not declarations winning the cascade.
enum class style_source_loss : uint32_t {
    unknown_declaration = 1u << 0, rejected_declaration = 1u << 1,
    malformed_declaration = 1u << 2, selector = 1u << 3,
    at_rule = 1u << 4, conditional_source = 1u << 5,
    external_source = 1u << 6, incomplete_parse = 1u << 7,
    budget = 1u << 8
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
    void loss(style_source_loss reason) noexcept { losses |= static_cast<uint32_t>(reason); }
    void incomplete(style_source_loss reason) noexcept { loss(reason); inputs_accounted = false; }
    void add(size_t& value, size_t increment, size_t limit) noexcept {
        if (value > limit || increment > limit - value) { value = limit; incomplete(style_source_loss::budget); }
        else value += increment;
    }
    void merge(const style_source_accounting_s& other) noexcept {
        add(source_bytes, other.source_bytes, max_source_bytes);
        add(declarations, other.declarations, max_declarations);
        add(rules, other.rules, max_rules);
        add(compiled_rules,other.compiled_rules,max_rules);
        add(expanded_declarations,other.expanded_declarations,max_declarations);
        add(compiled_bytes,other.compiled_bytes,max_compiled_bytes);
        losses |= other.losses;
        inputs_accounted = inputs_accounted && other.inputs_accounted;
        external_pending = external_pending || other.external_pending;
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
