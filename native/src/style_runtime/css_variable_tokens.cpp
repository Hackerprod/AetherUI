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
    // CSS consumes a final backslash as an escaped replacement character.
    return read(at) == '\\' && (at + 1 == length_ || !newline(read(at + 1)));
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
unsigned int css_variable_lexer::scalar() {
    const unsigned int first = read(cursor_++);
    if (first < 0x80) return first;
    const size_t continuation = first >= 0xc2 && first <= 0xdf ? 1 :
                                first >= 0xe0 && first <= 0xef ? 2 :
                                first >= 0xf0 && first <= 0xf4 ? 3 : 0;
    if (continuation == 0 || continuation > length_ - cursor_) {
        failed_ = true;
        return 0;
    }
    unsigned int value = first & (0x7f >> continuation);
    for (size_t i = 0; i < continuation; ++i) {
        const unsigned int c = read(cursor_);
        if (failed_ || (c & 0xc0) != 0x80) { failed_ = true; return 0; }
        ++cursor_;
        value = (value << 6) | (c & 0x3f);
    }
    const unsigned int minimum = continuation == 1 ? 0x80 : continuation == 2 ? 0x800 : 0x10000;
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) failed_ = true;
    return value;
}
unsigned int css_variable_lexer::escape() {
    ++cursor_; // The caller already recognized the backslash.
    if (cursor_ == length_) { escape_tail_ = css_variable_escape_tail::backslash; return 0xfffd; }
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
            else if (c == '\r' && cursor_ == length_) escape_tail_ = css_variable_escape_tail::hex_cr;
        }
        else if (cursor_ == length_) escape_tail_ = css_variable_escape_tail::hex;
        return value == 0 || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) ? 0xfffd : value;
    }
    return scalar();
}
void css_variable_lexer::name(bool& var, bool& url, bool& dashed,
                              css_identifier_sink sink, void* context) {
    size_t count = 0;
    var = url = true;
    dashed = true;
    while (cursor_ < length_ && !failed_) {
        unsigned int c = read(cursor_);
        if (named(static_cast<unsigned char>(c))) c = scalar();
        else if (escape_at(cursor_)) c = escape();
        else break;
        if (failed_) break;
        if (sink) {
            char bytes[4];
            size_t size = 0;
            if (c < 0x80) bytes[size++] = static_cast<char>(c);
            else if (c < 0x800) {
                bytes[size++] = static_cast<char>(0xc0 | (c >> 6));
                bytes[size++] = static_cast<char>(0x80 | (c & 0x3f));
            } else if (c < 0x10000) {
                bytes[size++] = static_cast<char>(0xe0 | (c >> 12));
                bytes[size++] = static_cast<char>(0x80 | ((c >> 6) & 0x3f));
                bytes[size++] = static_cast<char>(0x80 | (c & 0x3f));
            } else {
                bytes[size++] = static_cast<char>(0xf0 | (c >> 18));
                bytes[size++] = static_cast<char>(0x80 | ((c >> 12) & 0x3f));
                bytes[size++] = static_cast<char>(0x80 | ((c >> 6) & 0x3f));
                bytes[size++] = static_cast<char>(0x80 | (c & 0x3f));
            }
            if (!sink(context, bytes, size)) { failed_ = true; break; }
        }
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
bool css_variable_lexer::decode_identifier(css_identifier_sink sink, void* context) {
    if (!ident_at(cursor_) || failed_) return false;
    bool var, url, dashed;
    name(var, url, dashed, sink, context);
    return !failed_ && cursor_ == length_;
}
css_variable_token css_variable_lexer::next() {
    css_variable_token t;
    using category = css_variable_serialization;
    escape_tail_ = css_variable_escape_tail::none;
    t.begin = cursor_;
    auto finish = [&](css_variable_token_kind kind) {
        t.end = cursor_;
        t.escape_tail = escape_tail_;
        // Flags describe literal spelling, not decoded identifier values. Every
        // source access, including this bounded lookahead, is budgeted.
        t.starts_double_hyphen = t.end - t.begin >= 2 &&
            read(t.begin) == '-' && read(t.begin + 1) == '-';
        t.exact_double_hyphen = t.serialization == category::ident &&
            t.end - t.begin == 2 && t.starts_double_hyphen;
        t.kind = failed_ ? css_variable_token_kind::error : kind;
        // Recovery-aware readers need the original category (especially
        // comment trivia), while legacy readers still receive kind::error.
        // Budget/decoder failures must never become recoverable EOF tokens.
        t.eof_recoverable = t.eof_recoverable && !failed_;
        if (t.kind == css_variable_token_kind::error && !t.eof_recoverable)
            t.serialization = category::error;
        return t;
    };
    if (failed_) return finish(css_variable_token_kind::error);
    if (cursor_ >= length_) return finish(css_variable_token_kind::end);
    const auto c = read(cursor_);
    if (white(c)) {
        t.serialization = category::whitespace;
        do { ++cursor_; } while (cursor_ < length_ && white(read(cursor_)) && !failed_);
        return finish(css_variable_token_kind::whitespace);
    }
    if (c == '/' && read(cursor_ + 1) == '*') {
        t.serialization = category::comment;
        cursor_ += 2;
        while (cursor_ < length_ && !failed_) {
            if (read(cursor_) == '*' && read(cursor_ + 1) == '/') { cursor_ += 2; return finish(css_variable_token_kind::comment); }
            ++cursor_;
        }
        t.eof_recoverable = !failed_;
        return finish(css_variable_token_kind::error);
    }
    if (c == '\'' || c == '"') {
        t.serialization = category::string;
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
        t.eof_recoverable = !failed_;
        return finish(css_variable_token_kind::error);
    }
    // CSS sentinels take precedence over ident/delimiter tokenization.
    if (c == '-' && read(cursor_ + 1) == '-' && read(cursor_ + 2) == '>') {
        cursor_ += 3;
        t.serialization = category::cdc;
        return finish(css_variable_token_kind::other);
    }
    if (c == '<' && read(cursor_ + 1) == '!' && read(cursor_ + 2) == '-' && read(cursor_ + 3) == '-') {
        cursor_ += 4;
        t.serialization = category::cdo;
        return finish(css_variable_token_kind::other);
    }
    bool v = false, u = false, d = false;
    if (number_at(cursor_)) {
        t.serialization = category::number;
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
        t.numeric_end = cursor_;
        if (ident_at(cursor_)) { name(v, u, d); t.serialization = category::dimension; }
        else if (read(cursor_) == '%') { ++cursor_; t.serialization = category::percentage; }
        return finish(css_variable_token_kind::other);
    }
    if ((c == '#' && (named(read(cursor_ + 1)) || escape_at(cursor_ + 1))) || (c == '@' && ident_at(cursor_ + 1))) {
        ++cursor_;
        name(v, u, d);
        t.serialization = c == '#' ? category::hash : category::at_keyword;
        return finish(css_variable_token_kind::other);
    }
    if (ident_at(cursor_)) {
        name(v, u, d);
        t.dashed = d;
        t.serialization = category::ident;
        if (read(cursor_) != '(') return finish(css_variable_token_kind::ident);
        ++cursor_;
        t.serialization = category::function;
        if (u) {
            size_t look = cursor_;
            while (look < length_ && white(read(look)) && !failed_) ++look;
            const auto q = read(look);
            if (q != '\'' && q != '"') {
                t.serialization = category::url;
                cursor_ = look; // Leading whitespace is not URL content.
                bool trailing_white = false;
                while (cursor_ < length_ && !failed_) {
                    const auto a = read(cursor_);
                    if (a == ')') { ++cursor_; return finish(css_variable_token_kind::other); }
                    if (white(a)) {
                        trailing_white = true;
                        ++cursor_;
                        continue;
                    }
                    // Consume bad URL remnants as one opaque token too. Keep
                    // the legacy kind, but expose the error to syntax readers.
                    if (trailing_white || a == '\'' || a == '"' || a == '(' ||
                        a <= 8 || a == 11 || (a >= 14 && a <= 31) || a == 127 ||
                        (a == '\\' && !escape_at(cursor_)))
                        t.serialization = category::bad_url;
                    if (escape_at(cursor_)) { (void)escape(); }
                    else ++cursor_;
                }
                t.eof_recoverable = !failed_ && t.serialization == category::url;
                return finish(css_variable_token_kind::error);
            }
        }
        t.is_var = v;
        t.delimiter = '(';
        return finish(css_variable_token_kind::function);
    }
    ++cursor_;
    t.delimiter = static_cast<char>(c);
    t.serialization = c == '(' ? category::open_parenthesis :
        (c == '[' || c == '{' || c == ')' || c == ']' || c == '}' || c == ',') ? category::other : category::delimiter;
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

bool validate_pending_declaration_value(const std::string& source) {
    using kind = css_variable_token_kind;
    using category = css_variable_serialization;
    struct block {
        char close = 0;
        // A var() begins with precisely one dashed identifier, followed by
        // either its closing parenthesis or a comma and a declaration-value.
        enum class phase { ordinary, name, separator, fallback } state = phase::ordinary;
    };
    std::vector<block> blocks(1);
    css_variable_lexer lexer(source.data(), source.size());
    size_t components = 0;
    bool top_brace = false;
    for (;;) {
        const auto token = lexer.next();
        if ((token.kind == kind::error && !token.eof_recoverable) || token.serialization == category::bad_string ||
            token.serialization == category::bad_url) return false;
        if (token.kind == kind::end) {
            // CSS supplies missing block closes at EOF. A var() without a
            // name is still invalid; empty declarations/fallbacks are syntax,
            // leaving acceptance of the substituted value to property grammar.
            for (const auto& item : blocks)
                if (item.state == block::phase::name) return false;
            return true;
        }
        if (token.kind == kind::whitespace || token.kind == kind::comment ||
            (token.eof_recoverable && token.serialization == category::comment)) continue;
        if (token.kind == kind::close) {
            if (blocks.size() == 1 || token.delimiter != blocks.back().close ||
                blocks.back().state == block::phase::name) return false;
            blocks.pop_back();
            continue;
        }
        auto& current = blocks.back();
        if (current.state == block::phase::name) {
            if (token.kind != kind::ident || !token.dashed) return false;
            current.state = block::phase::separator;
            continue;
        }
        if (current.state == block::phase::separator) {
            if (token.kind != kind::comma) return false;
            current.state = block::phase::fallback;
            continue;
        }
        // Fallbacks have their own declaration-value boundary, even inside
        // another function. Nested ordinary blocks may contain these tokens.
        if ((blocks.size() == 1 || current.state == block::phase::fallback) &&
            token.kind == kind::other && token.serialization == category::delimiter &&
            (token.delimiter == '!' || token.delimiter == ';')) return false;
        if (blocks.size() == 1) {
            ++components;
            if (token.kind == kind::open && token.delimiter == '{') top_brace = true;
            if (top_brace && components > 1) return false;
        }
        if (token.kind == kind::open || token.kind == kind::function) {
            block nested;
            nested.close = token.delimiter == '(' ? ')' : token.delimiter == '[' ? ']' : '}';
            if (token.kind == kind::function && token.is_var) nested.state = block::phase::name;
            blocks.push_back(nested);
        }
    }
}
