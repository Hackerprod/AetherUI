#pragma once

#include <aether/aether.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <type_traits>

// Operation-local, pure line decisions. The caller owns text, style snapshots,
// owners and boxes; this class never measures text or invokes external code.
// Physical advances stay intact. Hanging determines alignment and the caller's private scroll proxy.
enum class inline_line_end { soft, forced, paragraph_end };
template<class Scalar>
struct basic_inline_line_metrics {
    Scalar width = Scalar(0), height = Scalar(0), baseline = Scalar(0);
    bool text = true;
    bool whitespace = false;
    int32_t white_space = AETHER_WHITE_SPACE_NORMAL;
    // Baseline participation is independent of text/glyph eligibility.
    bool atomic_baseline = false;
};
template<class Scalar>
struct basic_inline_line_summary {
    Scalar physical_width = Scalar(0), hanging_width = Scalar(0), alignment_width = Scalar(0);
    Scalar height = Scalar(0), ascent = Scalar(0);
    size_t justify_begin = 0, justify_end = 0;
};
template<class Scalar>
class basic_inline_line_resolver {
    static_assert(std::is_same_v<Scalar,float> || std::is_same_v<Scalar,double>);
    Scalar strut_ascent_ = Scalar(0), strut_descent_ = Scalar(0);
    Scalar available_, minimum_, physical_ = Scalar(0), height_, ascent_ = Scalar(0), descent_ = Scalar(0);
    std::vector<basic_inline_line_metrics<Scalar>> fragments_;
public:
    basic_inline_line_resolver(Scalar available, Scalar minimum)
        : available_(available), minimum_(minimum), height_(minimum) {}
    // Only the authenticated document adapter installs a physical parent strut.
    // Legacy callers leave this unset and retain their exact arithmetic.
    void set_physical_strut(Scalar height, Scalar baseline) {
        if (!fragments_.empty() || !std::isfinite(height) || !std::isfinite(baseline) ||
            height < Scalar(0) || baseline < Scalar(0) || baseline > height)
            throw std::invalid_argument("Invalid physical inline strut");
        strut_ascent_ = baseline; strut_descent_ = height - baseline;
        ascent_ = strut_ascent_; descent_ = strut_descent_;
        height_ = std::max(minimum_, height);
    }
    // Private document adapter reserves after charging the complete new capacity.
    size_t retained_capacity() const noexcept { return fragments_.capacity(); }
    void reserve_retained(size_t capacity) { fragments_.reserve(capacity); }
    bool empty() const { return fragments_.empty(); }
    Scalar physical_width() const { return physical_; }
    bool needs_break(Scalar unit_width, bool opportunity) const {
        return opportunity && !empty() && physical_ + unit_width > available_;
    }
    void append(basic_inline_line_metrics<Scalar> fragment) {
        const Scalar next = physical_ + fragment.width;
        if (!std::isfinite(next)) throw std::overflow_error("Non-finite inline advance");
        fragments_.push_back(fragment);
        physical_ = next;
        height_ = std::max(height_, fragment.height);
        if (fragment.text || fragment.atomic_baseline) {
            ascent_ = std::max(ascent_, fragment.baseline);
            descent_ = std::max(descent_, std::max(Scalar(0), fragment.height - fragment.baseline));
        }
    }
    basic_inline_line_summary<Scalar> close(inline_line_end end) const {
        basic_inline_line_summary<Scalar> out;
        out.physical_width = physical_;
        out.height = std::max(height_, ascent_ + descent_);
        out.ascent = ascent_;
        out.justify_end = fragments_.size();
        while (out.justify_begin < out.justify_end && fragments_[out.justify_begin].whitespace)
            ++out.justify_begin;
        while (out.justify_end > out.justify_begin && fragments_[out.justify_end - 1].whitespace)
            --out.justify_end;
        Scalar edge = physical_;
        for (auto it = fragments_.rbegin(); it != fragments_.rend(); ++it) {
            if (!it->text || !it->whitespace || it->white_space != AETHER_WHITE_SPACE_PRE_WRAP) break;
            const Scalar hanging = end == inline_line_end::soft ? it->width :
                std::min(it->width, std::max(Scalar(0), edge - available_));
            out.hanging_width += hanging;
            edge -= it->width;
            if (hanging < it->width) break;
        }
        // Compute A from its retained prefix, never P-H: a very large tail
        // can round P and H to the same Scalar and erase a small real word.
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
            Scalar prefix = Scalar(0);
            for (size_t i = 0; i < suffix_begin; ++i) prefix += fragments_[i].width;
            out.alignment_width = end == inline_line_end::soft ? prefix :
                std::max(prefix, std::min(physical_, available_));
        }
        return out;
    }
    void reset() {
        fragments_.clear(); physical_ = Scalar(0); height_ = (strut_ascent_ == Scalar(0) && strut_descent_ == Scalar(0)) ? minimum_ : std::max(minimum_, strut_ascent_ + strut_descent_); ascent_ = strut_ascent_; descent_ = strut_descent_;
    }
};

// Legacy aliases preserve float arithmetic, promotions and operation order.
using inline_line_metrics = basic_inline_line_metrics<float>;
using inline_line_summary = basic_inline_line_summary<float>;
using inline_line_resolver = basic_inline_line_resolver<float>;
// Inactive private document specialization: no layout callsites in this delta.
using document_inline_line_metrics = basic_inline_line_metrics<double>;
using document_inline_line_summary = basic_inline_line_summary<double>;
using document_inline_line_resolver = basic_inline_line_resolver<double>;
