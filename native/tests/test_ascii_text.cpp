// Byte contracts, independent of locale and active under NDEBUG.
#include "../src/primitives/ascii_text.h"
#include <cstdio>
#include <string>
#include <vector>
namespace {
unsigned checks = 0, failures = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL %s\n", label); }
}
unsigned char fold(unsigned char c) { return c >= 65 && c <= 90 ? c + 32 : c; }
}
int main() {
    using namespace aether;
    static_assert(ascii_lower_byte('I') == 'i', "constexpr ASCII fold");
    static_assert(is_html_space('\f') && !is_html_space('\v'), "HTML spaces");
    std::string all;
    for (unsigned a = 0; a != 256; ++a) {
        auto byte = static_cast<unsigned char>(a);
        all += static_cast<char>(byte);
        check(ascii_lower_byte(byte) == fold(byte), "all byte folds");
        check(ascii_lower_byte(ascii_lower_byte(byte)) == fold(byte), "idempotence");
        check(is_html_space(byte) == (a == 9 || a == 10 || a == 12 || a == 13 || a == 32), "five HTML spaces only");
        for (unsigned b = 0; b != 256; ++b) {
            const std::string x(1, static_cast<char>(a)), y(1, static_cast<char>(b));
            const bool equal = fold(byte) == fold(static_cast<unsigned char>(b));
            check(ascii_iequals(x, y) == equal, "all byte pair equalities including NUL");
            check((ascii_lowercase(x) == ascii_lowercase(y)) == equal, "key equivalence");
            check(ascii_istarts_with(x, y) == equal, "all byte prefixes");
            check(ascii_iends_with(x, y) == equal, "all byte suffixes");
            check(ascii_icontains(x, y) == equal, "all byte substrings");
        }
    }
    auto lowered = ascii_lowercase(all);
    check(lowered.size() == 256, "length preserved including NUL");
    for (unsigned i = 0; i != 256; ++i)
        check(static_cast<unsigned char>(lowered[i]) == fold(static_cast<unsigned char>(i)), "string fold bytes");
    check(ascii_lowercase(lowered) == lowered, "string fold idempotence");
    const std::vector<std::string> strings = {
        "", "I", "i", "@[{`", "\xc3\x84", "\xc3\xa4", "\xc4\xb0", "SS", "\xc3\x9f",
        "\xce\x91", "\xce\xb1", "\xc3\xa9", "e\xcc\x81", std::string("I\0A", 3),
        std::string("i\0a", 3), all, lowered, std::string(128, 'I'), std::string(128, 'i')
    };
    for (const auto& a : strings) for (const auto& b : strings) {
        const auto x = ascii_lowercase(a), y = ascii_lowercase(b);
        check(ascii_iequals(a, b) == (x == y), "string key equivalence");
        check(ascii_istarts_with(a, b) == (x.size() >= y.size() && x.compare(0, y.size(), y) == 0), "prefix contract");
        check(ascii_iends_with(a, b) == (x.size() >= y.size() && x.compare(x.size() - y.size(), y.size(), y) == 0), "suffix contract");
        check(ascii_icontains(a, b) == (x.find(y) != std::string::npos), "substring contract including empty");
    }
    check(ascii_icontains(std::string(128, 'x') + "END", "end"), "last offset search");
    check(!ascii_iequals("\xc3\x84", "\xc3\xa4"), "not Unicode folding");
    std::printf("ascii_text: %u checks, %u failures; char is %s\n", checks, failures, char(-1) < 0 ? "signed" : "unsigned");
    return failures ? 1 : 0;
}
