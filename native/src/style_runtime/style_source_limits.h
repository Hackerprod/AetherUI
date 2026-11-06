#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

// MAX is an exhausted next-ID sentinel, never an assigned rule ID or handle.
// Zero is retained for private compiler callers; runtime IDs start at one.
inline bool style_rule_ids_fit(int32_t next, size_t count) noexcept {
    return next >= 0 && count <= static_cast<size_t>(INT32_MAX - next);
}

inline bool style_sheet_handle_available(int64_t next) noexcept {
    return next >= 1 && next < INT64_MAX;
}

inline bool style_source_size_fits(size_t size) noexcept {
    return size <= static_cast<size_t>(INT32_MAX);
}
