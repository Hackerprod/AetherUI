#pragma once
#include <cstddef>
#include <string>

// Private span lexer. All byte access is through read(), including lookahead.
struct css_variable_budget {
    size_t remaining = 1048576;
    bool exhausted = false;
    bool take(size_t count) {
        if (count > remaining) { exhausted = true; return false; }
        remaining -= count;
        return true;
    }
};

enum class css_variable_token_kind { other, whitespace, comment, ident, function, open, close, comma, end, error };
// Serialization categories deliberately do not change the legacy kind used by
// invocation, !important and media readers.
enum class css_variable_serialization {
    other, ident, function, at_keyword, hash, number, percentage, dimension,
    url, bad_url, string, bad_string, delimiter, open_parenthesis, cdo, cdc,
    whitespace, comment, end, error
};
enum class css_variable_escape_tail { none, backslash, hex, hex_cr };
struct css_variable_token {
    css_variable_token_kind kind = css_variable_token_kind::end;
    size_t begin = 0, end = 0;
    // Absolute end of the numeric prefix for Number, Percentage and Dimension.
    size_t numeric_end = 0;
    char delimiter = 0;
    bool is_var = false, dashed = false;
    // Syntax consumers may recover EOF; legacy readers retain kind::error.
    bool eof_recoverable = false;
    css_variable_serialization serialization = css_variable_serialization::end;
    css_variable_escape_tail escape_tail = css_variable_escape_tail::none;
    bool starts_double_hyphen = false, exact_double_hyphen = false;
};
// A sink receives one decoded scalar (1..4 UTF-8 bytes) at a time. No allocation
// is performed by the decoder. A false return aborts the whole identifier.
using css_identifier_sink = bool (*)(void*, const char*, size_t);
class css_variable_lexer {
public:
    css_variable_lexer(const char* data, size_t length, size_t start = 0, css_variable_budget* budget = nullptr)
        : data_(data), length_(length), cursor_(start), budget_(budget) {}
    css_variable_token next();
    size_t position() const { return cursor_; }
    // Decode exactly one identifier spanning the remaining input, not arbitrary
    // escaped text. Callers must discard any partial sink output on failure.
    bool decode_identifier(css_identifier_sink sink, void* context);
    // Private CSS URL consumer. Preserve output on syntax/allocation failure.
    bool decode_url(std::string& out);
private:
    const char* data_;
    size_t length_, cursor_;
    css_variable_budget* budget_;
    bool failed_ = false;
    css_variable_escape_tail escape_tail_ = css_variable_escape_tail::none;
    unsigned char read(size_t at);
    bool escape_at(size_t at);
    bool ident_at(size_t at);
    bool number_at(size_t at);
    unsigned int scalar();
    unsigned int escape();
    void name(bool& var, bool& url, bool& dashed,
              css_identifier_sink sink = nullptr, void* context = nullptr);
};
bool variable_has_reference(const std::string& source);

// Validate an ordinary declaration value after its terminal !important has been
// removed. This checks all nested var() fallbacks without resolving variables or
// interpreting property grammar. Empty fallbacks are permitted.
bool validate_pending_declaration_value(const std::string& source);
