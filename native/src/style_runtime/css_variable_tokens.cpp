#include "css_variable_tokens.h"
#include <vector>

namespace {
bool white(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
bool newline(unsigned char c) { return c == '\n' || c == '\r' || c == '\f'; }
bool digit(unsigned char c) { return c >= '0' && c <= '9'; }
bool start(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 128; }
bool named(unsigned char c) { return start(c) || digit(c) || c == '-'; }
int hex(unsigned char c) {
    if (digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
}
unsigned char css_variable_lexer::read(size_t at) {
    if (at >= length_ || failed_) return 0;
    if (budget_ && !budget_->take(1)) { failed_ = true; return 0; }
    return static_cast<unsigned char>(data_[at]);
}
bool css_variable_lexer::escape_at(size_t at) {
    return read(at) == '\\' && at + 1 < length_ && !newline(read(at + 1));
}
bool css_variable_lexer::ident_at(size_t at) {
    const auto c = read(at);
    if (start(c) || escape_at(at)) return true;
    return c == '-' && (start(read(at + 1)) || read(at + 1) == '-' || escape_at(at + 1));
}
bool css_variable_lexer::number_at(size_t at) {
    auto c = read(at);
    if (c == '+' || c == '-') c = read(++at);
    return digit(c) || (c == '.' && digit(read(at + 1)));
}
unsigned int css_variable_lexer::escape() {
    ++cursor_; // The caller already recognized the backslash.
    unsigned int value = 0;
    if (hex(read(cursor_)) >= 0) {
        for (size_t n = 0; n < 6 && cursor_ < length_ && !failed_; ++n) {
            const int h = hex(read(cursor_));
            if (h < 0) break;
            value = value * 16 + static_cast<unsigned int>(h);
            ++cursor_;
        }
        if (white(read(cursor_))) {
            const auto c = read(cursor_++);
            if (c == '\r' && read(cursor_) == '\n') ++cursor_;
        }
        return value == 0 || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) ? 0xfffd : value;
    }
    return read(cursor_++);
}
void css_variable_lexer::name(bool& var, bool& url, bool& dashed) {
    size_t count = 0;
    var = url = true;
    dashed = true;
    while (cursor_ < length_ && !failed_) {
        unsigned int c = read(cursor_);
        if (named(static_cast<unsigned char>(c))) ++cursor_;
        else if (escape_at(cursor_)) c = escape();
        else break;
        if (count < 2 && c != '-') dashed = false;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (count >= 3 || c != static_cast<unsigned int>("var"[count])) var = false;
        if (count >= 3 || c != static_cast<unsigned int>("url"[count])) url = false;
        ++count;
    }
    var = var && count == 3;
    url = url && count == 3;
    dashed = dashed && count > 2;
}
css_variable_token css_variable_lexer::next() {
    css_variable_token t;
    t.begin = cursor_;
    auto finish = [&](css_variable_token_kind kind) {
        t.kind = failed_ ? css_variable_token_kind::error : kind;
        t.end = cursor_;
        return t;
    };
    if (failed_) return finish(css_variable_token_kind::error);
    if (cursor_ >= length_) return finish(css_variable_token_kind::end);
    const auto c = read(cursor_);
    if (white(c)) {
        do { ++cursor_; } while (cursor_ < length_ && white(read(cursor_)) && !failed_);
        return finish(css_variable_token_kind::whitespace);
    }
    if (c == '/' && read(cursor_ + 1) == '*') {
        cursor_ += 2;
        while (cursor_ < length_ && !failed_) {
            if (read(cursor_) == '*' && read(cursor_ + 1) == '/') { cursor_ += 2; return finish(css_variable_token_kind::comment); }
            ++cursor_;
        }
        return finish(css_variable_token_kind::error);
    }
    if (c == '\'' || c == '"') {
        ++cursor_;
        while (cursor_ < length_ && !failed_) {
            const auto q = read(cursor_++);
            if (q == c) return finish(css_variable_token_kind::other);
            if (newline(q)) return finish(css_variable_token_kind::error);
            if (q == '\\') {
                if (cursor_ >= length_) break;
                const auto e = read(cursor_++);
                if (e == '\r' && read(cursor_) == '\n') ++cursor_;
            }
        }
        return finish(css_variable_token_kind::error);
    }
    bool v = false, u = false, d = false;
    if (number_at(cursor_)) {
        if (c == '+' || c == '-') ++cursor_;
        while (digit(read(cursor_)) && !failed_) ++cursor_;
        if (read(cursor_) == '.' && digit(read(cursor_ + 1))) {
            ++cursor_;
            while (digit(read(cursor_)) && !failed_) ++cursor_;
        }
        const auto e = read(cursor_);
        size_t exponent = cursor_ + 1;
        if (read(exponent) == '+' || read(exponent) == '-') ++exponent;
        if ((e == 'e' || e == 'E') && digit(read(exponent))) {
            cursor_ = exponent;
            while (digit(read(cursor_)) && !failed_) ++cursor_;
        }
        if (ident_at(cursor_)) name(v, u, d);
        else if (read(cursor_) == '%') ++cursor_;
        return finish(css_variable_token_kind::other);
    }
    if ((c == '#' && (named(read(cursor_ + 1)) || escape_at(cursor_ + 1))) || (c == '@' && ident_at(cursor_ + 1))) {
        ++cursor_;
        name(v, u, d);
        return finish(css_variable_token_kind::other);
    }
    if (ident_at(cursor_)) {
        name(v, u, d);
        t.dashed = d;
        if (read(cursor_) != '(') return finish(css_variable_token_kind::ident);
        ++cursor_;
        if (u) {
            size_t look = cursor_;
            while (look < length_ && white(read(look)) && !failed_) ++look;
            const auto q = read(look);
            if (q != '\'' && q != '"') {
                while (cursor_ < length_ && !failed_) {
                    const auto a = read(cursor_);
                    if (a == ')') { ++cursor_; return finish(css_variable_token_kind::other); }
                    if (escape_at(cursor_)) { (void)escape(); }
                    else ++cursor_;
                }
                return finish(css_variable_token_kind::error);
            }
        }
        t.is_var = v;
        t.delimiter = '(';
        return finish(css_variable_token_kind::function);
    }
    ++cursor_;
    t.delimiter = static_cast<char>(c);
    if (c == '(' || c == '[' || c == '{') return finish(css_variable_token_kind::open);
    if (c == ')' || c == ']' || c == '}') return finish(css_variable_token_kind::close);
    if (c == ',') return finish(css_variable_token_kind::comma);
    return finish(css_variable_token_kind::other);
}
bool variable_has_reference(const std::string& source) {
    css_variable_lexer lexer(source.data(), source.size());
    for (;;) {
        const auto t = lexer.next();
        if (t.kind == css_variable_token_kind::function && t.is_var) return true;
        if (t.kind == css_variable_token_kind::end || t.kind == css_variable_token_kind::error) return false;
    }
}
