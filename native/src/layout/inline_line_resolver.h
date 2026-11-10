#pragma once

#include <aether/aether.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

// Operation-local, pure line decisions. The caller owns text, style snapshots,
// owners and boxes; this class never measures text or invokes external code.
// Physical advances stay intact. Hanging determines alignment and the caller's private scroll proxy.
enum class inline_line_end { soft, forced, paragraph_end };
struct inline_line_metrics {
    float width = 0.f, height = 0.f, baseline = 0.f;
    bool text = true;
    bool whitespace = false;
    int32_t white_space = AETHER_WHITE_SPACE_NORMAL;
};
struct inline_line_summary {
    float physical_width = 0.f, hanging_width = 0.f, alignment_width = 0.f;
    float height = 0.f, ascent = 0.f;
    size_t justify_begin = 0, justify_end = 0;
};
class inline_line_resolver {
    float available_, minimum_, physical_ = 0.f, height_, ascent_ = 0.f, descent_ = 0.f;
    std::vector<inline_line_metrics> fragments_;
public:
    inline_line_resolver(float available, float minimum)
        : available_(available), minimum_(minimum), height_(minimum) {}
    bool empty() const { return fragments_.empty(); }
    float physical_width() const { return physical_; }
    bool needs_break(float unit_width, bool opportunity) const {
        return opportunity && !empty() && physical_ + unit_width > available_;
    }
    void append(inline_line_metrics fragment) {
        const float next = physical_ + fragment.width;
        if (!std::isfinite(next)) throw std::overflow_error("Non-finite inline advance");
        fragments_.push_back(fragment);
        physical_ = next;
        height_ = std::max(height_, fragment.height);
        if (fragment.text) {
            ascent_ = std::max(ascent_, fragment.baseline);
            descent_ = std::max(descent_, std::max(0.f, fragment.height - fragment.baseline));
        }
    }
    inline_line_summary close(inline_line_end end) const {
        inline_line_summary out;
        out.physical_width = physical_;
        out.height = std::max(height_, ascent_ + descent_);
        out.ascent = ascent_;
        out.justify_end = fragments_.size();
        while (out.justify_begin < out.justify_end && fragments_[out.justify_begin].whitespace)
            ++out.justify_begin;
        while (out.justify_end > out.justify_begin && fragments_[out.justify_end - 1].whitespace)
            --out.justify_end;
        float edge = physical_;
        for (auto it = fragments_.rbegin(); it != fragments_.rend(); ++it) {
            if (!it->text || !it->whitespace || it->white_space != AETHER_WHITE_SPACE_PRE_WRAP) break;
            const float hanging = end == inline_line_end::soft ? it->width :
                std::min(it->width, std::max(0.f, edge - available_));
            out.hanging_width += hanging;
            edge -= it->width;
            if (hanging < it->width) break;
        }
        // Compute A from its retained prefix, never P-H: a very large tail
        // can round P and H to the same float and erase a small real word.
        // P and the fit decisions deliberately retain their physical domain.
        size_t suffix_begin = fragments_.size();
        while (suffix_begin > 0) {
            const auto& fragment = fragments_[suffix_begin - 1];
            if (!fragment.text || !fragment.whitespace ||
                fragment.white_space != AETHER_WHITE_SPACE_PRE_WRAP) break;
            --suffix_begin;
        }
        out.alignment_width = physical_;
        if (suffix_begin < fragments_.size()) {
            float prefix = 0.f;
            for (size_t i = 0; i < suffix_begin; ++i) prefix += fragments_[i].width;
            out.alignment_width = end == inline_line_end::soft ? prefix :
                std::max(prefix, std::min(physical_, available_));
        }
        return out;
    }
    void reset() {
        fragments_.clear(); physical_ = 0.f; height_ = minimum_; ascent_ = descent_ = 0.f;
    }
};
