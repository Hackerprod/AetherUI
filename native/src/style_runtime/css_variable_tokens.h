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
struct css_variable_token {
    css_variable_token_kind kind = css_variable_token_kind::end;
    size_t begin = 0, end = 0;
    char delimiter = 0;
    bool is_var = false, dashed = false;
};
class css_variable_lexer {
public:
    css_variable_lexer(const char* data, size_t length, size_t start = 0, css_variable_budget* budget = nullptr)
        : data_(data), length_(length), cursor_(start), budget_(budget) {}
    css_variable_token next();
    size_t position() const { return cursor_; }
private:
    const char* data_;
    size_t length_, cursor_;
    css_variable_budget* budget_;
    bool failed_ = false;
    unsigned char read(size_t at);
    bool escape_at(size_t at);
    bool ident_at(size_t at);
    bool number_at(size_t at);
    unsigned int escape();
    void name(bool& var, bool& url, bool& dashed);
};
bool variable_has_reference(const std::string& source);
