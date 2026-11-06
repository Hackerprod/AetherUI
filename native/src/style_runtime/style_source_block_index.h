#pragma once

#include "css_component_tokens.h"
#include <cstdint>
#include <string>
#include <vector>

// Body bounds and cursor advancement are deliberately different. A real close
// excludes the closing token from the body but consumes it; EOF consumes no
// imaginary token. Callers have already checked source.size() <= INT32_MAX.
struct style_source_block_extent_s {
    int32_t open;
    int32_t body_end;
    int32_t next;
};

// Optional private instrumentation for structural complexity tests, not an ABI.
struct style_source_block_work_s {
    size_t token_visits = 0;
    size_t extent_visits = 0;
};

class style_source_block_index_s {
public:
    explicit style_source_block_index_s(const std::string& source,
                                        style_source_block_work_s* work = nullptr)
        : work_(work) {
        struct component_s { char close; size_t extent; };
        std::vector<component_s> pending;
        const auto end = static_cast<int32_t>(source.size());
        css_component_lexer lexer(source.data(), source.size());
        for (;;) {
            const auto token = lexer.next();
            if (token.kind == css_variable_token_kind::end) break;
            if (work_) ++work_->token_visits;
            if (token.kind == css_variable_token_kind::open ||
                token.kind == css_variable_token_kind::function) {
                const auto entry = extents_.size();
                if (token.delimiter == '{')
                    extents_.push_back({static_cast<int32_t>(token.begin), end, end});
                pending.push_back({token.delimiter == '(' ? ')' : token.delimiter == '[' ? ']' : '}', entry});
            } else if (token.kind == css_variable_token_kind::close && !pending.empty() &&
                       pending.back().close == token.delimiter) {
                // A mismatched close is content. It cannot pop an ancestor.
                const auto component = pending.back();
                pending.pop_back();
                if (component.close == '}') {
                    auto& extent = extents_[component.extent];
                    extent.body_end = static_cast<int32_t>(token.begin);
                    extent.next = static_cast<int32_t>(token.end);
                }
            }
        }
        // Unclosed braces already carry EOF extents, including those below an
        // unclosed function/bracket. No source bytes or offsets are fabricated.
    }

    const style_source_block_extent_s* find(int32_t opening) {
        // Iterative DFS requests openings in strictly increasing source order.
        // Skip ignored bodies/prelude components only when the NEXT opening is
        // requested; advancing to a parent's next would skip its unborn child.
        while (cursor_ < extents_.size() && extents_[cursor_].open < opening) {
            ++cursor_;
            if (work_) ++work_->extent_visits;
        }
        if (cursor_ == extents_.size() || extents_[cursor_].open != opening) return nullptr;
        if (work_) ++work_->extent_visits;
        return &extents_[cursor_++];
    }

private:
    std::vector<style_source_block_extent_s> extents_;
    size_t cursor_ = 0;
    style_source_block_work_s* work_;
};
