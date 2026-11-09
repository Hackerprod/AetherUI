#include "inline_text.h"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>

// Count allocations only around the predicate, never input or oracle setup.
// This executable compiles inline_text.cpp directly, so the actual string/vector
// allocation calls resolve to these replacements rather than a hidden DSO copy.
namespace {
bool count_allocations = false;
size_t allocations = 0;
size_t cases = 0;

void* allocate(size_t size) {
    if (count_allocations) ++allocations;
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
}

void* operator new(size_t size) { return allocate(size); }
void* operator new[](size_t size) { return allocate(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, size_t) noexcept { std::free(memory); }

namespace {
bool token_visibility(const std::string& text, int32_t mode) {
    for (const auto& token : aether_tokenize_inline_text(text, mode)) {
        if (token.kind == aether_inline_text_token_kind_e::word ||
            token.kind == aether_inline_text_token_kind_e::preserved_space ||
            token.kind == aether_inline_text_token_kind_e::forced_break) return true;
    }
    return false;
}

void check(const std::string& text, int32_t mode) {
    const bool expected = token_visibility(text, mode);
    allocations = 0;
    count_allocations = true;
    const bool actual = aether_inline_text_has_visible_content(text, mode);
    count_allocations = false;
    if (actual != expected || allocations != 0) {
        std::fprintf(stderr,
            "FAIL: visibility mode=%d length=%zu first=%u last=%u expected=%d actual=%d allocations=%zu\n",
            static_cast<int>(mode), text.size(),
            text.empty() ? 0u : static_cast<unsigned char>(text.front()),
            text.empty() ? 0u : static_cast<unsigned char>(text.back()),
            expected, actual, allocations);
        std::exit(1);
    }
    ++cases;
}
}

int main() {
    // Positive control: the old tokenizer-backed implementation allocates.
    const std::string control(4096, 'x');
    count_allocations = true;
    const bool control_visible = token_visibility(control, AETHER_WHITE_SPACE_NORMAL);
    count_allocations = false;
    if (!control_visible || allocations == 0) {
        std::fprintf(stderr, "FAIL: allocation counter positive control\n");
        return 1;
    }

    const int32_t modes[] = {
        AETHER_WHITE_SPACE_NORMAL, AETHER_WHITE_SPACE_NOWRAP,
        AETHER_WHITE_SPACE_PRE, AETHER_WHITE_SPACE_PRE_WRAP,
        AETHER_WHITE_SPACE_PRE_LINE, -1,
        std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()
    };
    for (const int32_t mode : modes) {
        check("", mode);
        std::string bytes(2, '\0');
        for (unsigned first = 0; first < 256; ++first) {
            check(std::string(1, static_cast<char>(first)), mode);
            bytes[0] = static_cast<char>(first);
            for (unsigned second = 0; second < 256; ++second) {
                bytes[1] = static_cast<char>(second);
                check(bytes, mode);
            }
        }
        for (const std::string value : {
                "\r\n", "\r\r\n", "\n\r", " \t\v\f\r\n", "\r\n \t\r\n",
                "\xC2\xA0", "\xE2\x80\x83", "\x80\xFF"}) {
            check(value, mode);
        }

        // Exercise complete scans, visible bytes at both ends and the middle,
        // long words/spaces, CRLF, and embedded NUL/high bytes without C strings.
        std::string spaces;
        spaces.reserve(131072);
        for (size_t i = 0; i < 32768; ++i) spaces += " \t\v\f";
        check(spaces, mode);
        for (const char visible : {'x', '\0', static_cast<char>(0x80), static_cast<char>(0xFF), '\r', '\n'}) {
            for (const size_t position : {size_t{0}, spaces.size() / 2, spaces.size() - 1}) {
                const char saved = spaces[position];
                spaces[position] = visible;
                check(spaces, mode);
                spaces[position] = saved;
            }
        }
        std::string breaks;
        breaks.reserve(131072);
        for (size_t i = 0; i < 65536; ++i) breaks += "\r\n";
        check(breaks, mode);
        check(std::string(131072, 'x'), mode);
        check(std::string(131072, '\0'), mode);
    }
    std::printf("PASS: %zu tokenizer comparisons; zero predicate allocations in every call\n", cases);
    return 0;
}
