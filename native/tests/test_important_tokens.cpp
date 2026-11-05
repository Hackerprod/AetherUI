#include "css_important.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// Exercise the production extractor directly. Public computed styles cannot
// reveal every false priority or lost source byte when a value is later invalid.
static size_t checks = 0;
static void expect(const std::string& input, bool priority, const std::string& expected) {
    std::string actual = input;
    const bool important = css_strip_important(actual);
    ++checks;
    if (important != priority || actual != expected) {
        std::fprintf(stderr, "important token check %zu failed: priority %d (expected %d), "
            "value [%s] (%zu bytes, expected [%s], %zu bytes), input [%s]\n",
            checks, important, priority, actual.c_str(), actual.size(), expected.c_str(),
            expected.size(), input.c_str());
        std::exit(1);
    }
}

int main() {
    const std::vector<std::string> valid = {
        "!important", "!ImPoRtAnT", "! important", "!\timportant", "!\nimportant",
        "!\fimportant", "!\rimportant", "!\r\nimportant", "! \t\n\f\r important",
        "!/**/important", "! /*priority*/ important", "!/**/ /**/important/**/",
        "!important/*tail*/", "!important \t\n\f\r\n", R"(!\69mportant)",
        R"(!\000069mportant)", R"(!\69 mportant)", R"(!impor\74 ant)",
        R"(!importan\74 )", R"(!\important)", "!\\69\tmportant", "!\\69\nmportant",
        "!\\69\fmportant", "!\\69\rmportant", "!\\69\r\nmportant",
        R"(!\49M\50ORTANT)", R"(!\69\6d\70\6f\72\74\61\6e\74 )"
    };
    const std::vector<std::string> payloads = {
        "37px", "MiXeD", R"("MiXeD /* !important")", R"('escaped\' !important')",
        R"(url(asset!important))", R"(url("asset !important"))", "fn([x] { y })",
        "[x] { y }", R"(foo\ )", R"(foo\20 )", R"(foo\\)"
    };
    for (const auto& marker : valid) {
        expect("37px" + marker, true, "37px");
        expect(" \f/**/ " + marker + " /**/\t", true, "");
        for (const auto& payload : payloads)
            expect("\f " + payload + " \t" + marker + " /**/\r\n", true, payload);
    }

    const std::vector<std::string> invalid = {
        " important", "!importantx", "!important-", "!important()", "!important/**/()",
        "!important 3px", "!important !", "!#important", "!@important", "!1important",
        "!im/**/portant", "!im portant", R"(!\69  mportant)", R"(!\69/**/mportant)",
        R"(\!important)", R"(\21 important)", R"(!\69mportant\ )", R"(!\0 mportant)",
        R"(!\d800 mportant)", R"(!\110000 mportant)", "!\vimportant",
        "!\xc2\xa0important", "!\\\nimportant", "!\\\r\nimportant", "!\\\fimportant",
        "!\xc4\xb0mportant", "!impo\xc5\x80tant", "!important[]", "!important{}",
        "!important fn()", "!important/**/[]", "!important,", "!important/",
        "!\\69\t mportant", "!\\69\n mportant", "!\\69\f mportant",
        "!\\69\r mportant", "!\\69\r\n mportant"
    };
    for (const auto& marker : invalid) expect("37px " + marker, false, "37px " + marker);

    const std::vector<std::string> atomic = {
        R"("!important")", R"('!important')", R"("escaped\" !important")",
        R"("/* !important */")", R"(url(asset!important))", R"(url("!important"))",
        R"(url(a\)!important))", "fn(!important)", "fn([!important] { !important })",
        "[!important]", "{!important}", "var(--x, ! important)", "[fn()] { [x] }"
    };
    for (const auto& value : atomic) {
        expect("\f " + value + "\t", false, value);
        expect(value + " ! important", true, value);
    }

    // Invalid/unclosed components never grant priority, and their contents are
    // not trimmed as outer trivia. Include a marker after an unmatched close.
    const std::vector<std::string> malformed = {
        "fn(!important", "[!important", "{!important", "(37px !important ",
        "fn(37px) [!important ", "[) !important", "(] !important", "{] !important",
        ") !important", "] !important", "} !important", "37px !important [",
        "37px !important {", "37px !important fn(", "\"!important", "'!important",
        "url(!important", "url(37px !important ", "\"bad\nstring\" !important",
        "/* !important", "37px ! important /* open", std::string("\xff !important", 12)
    };
    for (const auto& value : malformed) expect(value, false, value);

    expect("", false, "");
    expect(" \t\n\f\r\n/**/", false, "");
    expect("\fMiXeD\f", false, "MiXeD");
    expect("37px /**/ !important", true, "37px");
    expect("37px/**/ 42px !important", true, "37px/**/ 42px");
    expect(R"(foo\ )", false, R"(foo\ )");
    expect(R"(foo\20 )", false, R"(foo\20 )");
    expect("foo\\20\r\n", false, "foo\\20\r\n");
    expect("foo\\20\r\n !important", true, "foo\\20\r\n");
    expect(R"(foo\ ! important)", true, R"(foo\ )");
    expect(R"(foo\20 ! important)", true, R"(foo\20 )");
    expect(R"(foo\\!important)", true, R"(foo\\)");
    expect(R"(foo\\\!important)", false, R"(foo\\\!important)");

    // Extraction is not value validation. Remaining punctuation must survive
    // for the consuming property parser to reject it.
    expect("37px !!important", true, "37px !");
    expect("37px !important !important", true, "37px !important");
    expect("!important !important", true, "!important");

    // The scanner is iterative even for deeply nested components.
    const std::string deep = std::string(4096, '(') + "!important" + std::string(4096, ')');
    expect(deep, false, deep);
    expect(deep + " ! important", true, deep);
    expect(std::string(4096, '(') + "!important", false, std::string(4096, '(') + "!important");
    std::printf("CSS important tokens: %zu checks passed\n", checks);
    return 0;
}
