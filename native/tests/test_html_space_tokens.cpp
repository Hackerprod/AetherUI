// Length-aware scanner only: embedded NUL here is not a new C ABI promise.
#include "../src/primitives/html_space_tokens.h"
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>
namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL %s\n", label); }
}
bool space(unsigned byte) { return byte == 9 || byte == 10 || byte == 12 || byte == 13 || byte == 32; }
void verify(std::string_view source) {
    // Independent numeric oracle, deliberately not using is_html_space.
    std::vector<std::string_view> expected;
    for (size_t i = 0; i < source.size();) {
        if (space(static_cast<unsigned char>(source[i]))) { ++i; continue; }
        const size_t start = i++;
        while (i < source.size() && !space(static_cast<unsigned char>(source[i]))) ++i;
        expected.push_back(source.substr(start, i - start));
    }
    aether::html_space_token_scanner scanner(source);
    std::string_view actual;
    size_t count = 0;
    while (scanner.next(actual)) {
        check(count < expected.size(), "no extra tokens");
        if (count < expected.size()) {
            check(actual == expected[count], "exact token bytes and length");
            check(actual.data() == expected[count].data(), "token borrows exact source span");
        }
        check(!actual.empty(), "never emit empty token");
        check(aether::html_space_contains(source, actual), "exact token membership");
        ++count;
    }
    check(count == expected.size(), "complete token count");
    check(actual.empty(), "exhaustion clears output view");
    check(!scanner.next(actual) && actual.empty(), "exhaustion is stable");
    check(!aether::html_space_contains(source, ""), "empty query never matches");
    check(!aether::html_space_contains(source, "a B"), "query containing HTML space never matches");
}
}
int main() {
    static_assert(noexcept(aether::html_space_token_scanner(std::string_view{})));
    static_assert(noexcept(std::declval<aether::html_space_token_scanner&>().next(std::declval<std::string_view&>())));
    static_assert(noexcept(aether::html_space_contains({}, {})));
    static_assert(aether::html_space_contains(" a\vB \t", "a\vB"));
    static_assert(!aether::html_space_contains("a\vB", "a"));
    verify({});
    for (const auto& text : {std::string(""), std::string(" \t\n\f\r"), std::string("a"), std::string("a a a"), std::string("\t\t a\fB\nC \r"),
                             std::string("a\0B", 3), std::string("\0 \0", 3), std::string("a\xc2\xa0" "B a\xc2\x85" "B a\xe2\x80\x83" "B"),
                             std::string(1024 * 1024, 'x')}) verify(text);
    std::string all;
    for (unsigned byte = 0; byte < 256; ++byte) {
        all += static_cast<char>(byte);
        const std::string value = "a" + std::string(1, static_cast<char>(byte)) + "B";
        verify(value);
        check(aether::html_space_contains(value, "a") == space(byte), "all 256 separator bytes");
        check(aether::html_space_contains(value, "B") == space(byte), "all 256 separator bytes trailing");
        check(aether::html_space_contains(value, value) == !space(byte), "all 256 whole token bytes");
        for (unsigned second = 0; second < 256; ++second) {
            const std::string pair = "\t" + std::string(1, static_cast<char>(byte)) + std::string(1, static_cast<char>(second)) + "\r";
            verify(pair);
        }
    }
    verify(all);
    check(aether::html_space_contains("I i", "I") && !aether::html_space_contains("I", "i"), "exact ASCII case");
    check(!aether::html_space_contains("\xc3\x84", "\xc3\xa4"), "no Unicode case fold");
    check(!aether::html_space_contains("prefix-suffix", "prefix"), "no partial token match");
    std::printf("html_space_tokens: %u checks, %u failures; char is %s\n", checks, failures, char(-1) < 0 ? "signed" : "unsigned");
    return failures ? 1 : 0;
}
