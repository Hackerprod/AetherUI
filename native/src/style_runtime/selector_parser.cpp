#include "style_runtime_types.h"
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <charconv>

/* ── Helpers ──────────────────────────────────────────────────────── */

static std::string to_lower_ascii(std::string_view s) {
    std::string result(s);
    for (auto& c : result)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return result;
}

static bool is_css_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

// Comments are token separators, never whitespace. Keep this out of name,
// escape and string consumers: their source bytes belong to one atomic token.
static bool consume_comment(std::string_view source, size_t& index) {
    if (index + 1 >= source.size() || source[index] != '/' || source[index + 1] != '*')
        return false;
    const size_t closing = source.find("*/", index + 2);
    index = closing == std::string_view::npos ? source.size() : closing + 2;
    return true;
}

static void skip_comments(std::string_view source, size_t& index) {
    while (consume_comment(source, index)) {}
}

static bool skip_trivia(std::string_view source, size_t& index) {
    bool whitespace = false;
    while (index < source.size()) {
        if (is_css_space(source[index])) { whitespace = true; ++index; }
        else if (!consume_comment(source, index)) break;
    }
    return whitespace;
}

static bool is_css_newline(char c) {
    return c == '\n' || c == '\r' || c == '\f';
}

static bool is_identifier_start(char c) {
    const auto u = static_cast<unsigned char>(c);
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           c == '_' || c == '-' || u >= 128;
}

static bool is_identifier_part(char c) {
    return is_identifier_start(c) || (c >= '0' && c <= '9');
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void append_scalar(std::string* out, uint32_t value) {
    if (!out) return;
    if (value <= 0x7f) {
        out->push_back(static_cast<char>(value));
    } else if (value <= 0x7ff) {
        out->push_back(static_cast<char>(0xc0 | (value >> 6)));
        out->push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else if (value <= 0xffff) {
        out->push_back(static_cast<char>(0xe0 | (value >> 12)));
        out->push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        out->push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else {
        out->push_back(static_cast<char>(0xf0 | (value >> 18)));
        out->push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
        out->push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        out->push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
}

// Preserve valid source UTF-8 exactly. On malformed input advance at least one byte,
// so structural recovery is bounded; the semantic consumer rejects that branch.
static bool consume_scalar(std::string_view source, size_t& index, std::string* out) {
    if (index >= source.size()) return false;
    const size_t start = index++;
    const auto first = static_cast<unsigned char>(source[start]);
    if (first < 0x80) {
        if (first == 0) return false;
        if (out) out->push_back(static_cast<char>(first));
        return true;
    }
    const size_t length = first >= 0xc2 && first <= 0xdf ? 2 :
                          first >= 0xe0 && first <= 0xef ? 3 :
                          first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    if (!length || source.size() - start < length) return false;
    uint32_t scalar = first & (0x7f >> length);
    for (size_t n = 1; n < length; ++n) {
        const auto byte = static_cast<unsigned char>(source[start + n]);
        if ((byte & 0xc0) != 0x80) return false;
        scalar = (scalar << 6) | (byte & 0x3f);
    }
    if ((length == 2 && scalar < 0x80) || (length == 3 && scalar < 0x800) ||
        (length == 4 && scalar < 0x10000) || scalar > 0x10ffff ||
        (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
    index = start + length;
    if (out) out->append(source.substr(start, length));
    return true;
}

static void consume_newline(std::string_view source, size_t& index) {
    const char c = source[index++];
    if (c == '\r' && index < source.size() && source[index] == '\n') ++index;
}

// Both decoding and boundary scanning call this primitive. Decoded punctuation
// is name/string data and is never examined again as selector structure.
static bool consume_escape(std::string_view source, size_t& index,
                           std::string* out, bool in_string) {
    ++index; // backslash
    if (index == source.size()) {
        if (!in_string) append_scalar(out, 0xfffd);
        return true;
    }
    if (is_css_newline(source[index])) {
        consume_newline(source, index);
        return in_string;
    }
    if (hex_digit(source[index]) >= 0) {
        uint32_t value = 0;
        unsigned digits = 0;
        while (index < source.size() && digits < 6 && hex_digit(source[index]) >= 0) {
            value = (value << 4) | static_cast<unsigned>(hex_digit(source[index++]));
            ++digits;
        }
        if (index < source.size() && is_css_space(source[index])) {
            if (source[index] == '\r') consume_newline(source, index);
            else ++index;
        }
        if (!value || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) value = 0xfffd;
        append_scalar(out, value);
        return true;
    }
    return consume_scalar(source, index, out);
}

static bool consume_name(std::string_view source, size_t& index, std::string* out) {
    const size_t start = index;
    while (index < source.size()) {
        if (source[index] == '\\') {
            if (!consume_escape(source, index, out, false)) return false;
        } else if (is_identifier_part(source[index])) {
            if (!consume_scalar(source, index, out)) return false;
        } else break;
    }
    return index > start;
}

// A bad-string newline ends the token, leaving real following delimiters visible
// to a forgiving enclosing selector list. No later quote is invented as its end.
static bool consume_string(std::string_view source, size_t& index, std::string* out) {
    const char quote = source[index++];
    bool valid = true;
    while (index < source.size()) {
        const char c = source[index];
        if (c == quote) { ++index; return valid; }
        if (is_css_newline(c)) { consume_newline(source, index); return false; }
        if (c == '\\') {
            if (!consume_escape(source, index, out, true)) valid = false;
        } else if (!consume_scalar(source, index, out)) valid = false;
    }
    return false; // Preserve the existing rejection of unterminated attributes.
}

// Yield only unescaped structural source bytes. Lexical errors remain local to
// the semantic parser owning a branch, not to its enclosing forgiving function.
static bool next_structure(std::string_view source, size_t& index, size_t& offset, char& token) {
    while (index < source.size()) {
        const char c = source[index];
        if (consume_comment(source, index)) {
            continue;
        } else if (c == '\'' || c == '"') {
            consume_string(source, index, nullptr);
        } else if (is_identifier_part(c) || c == '\\') {
            consume_name(source, index, nullptr);
        } else {
            offset = index++;
            token = c;
            return true;
        }
    }
    return false;
}

static char matching_close(char c) {
    if (c == '(') return ')';
    if (c == '[') return ']';
    if (c == '{') return '}';
    return '\0';
}

static void update_blocks(char c, std::vector<char>& blocks) {
    if (const char closing = matching_close(c)) blocks.push_back(closing);
    else if (!blocks.empty() && c == blocks.back()) blocks.pop_back();
    // A mismatched close never closes another block. The semantic branch parser
    // rejects it; scanning continues to the actual matching delimiter, if any.
}

static std::vector<std::string_view> split_top_level(std::string_view source, char sep) {
    std::vector<std::string_view> result;
    std::vector<char> blocks;
    size_t start = 0, index = 0, offset = 0;
    char token = '\0';
    while (next_structure(source, index, offset, token)) {
        if (token == sep && blocks.empty()) {
            result.push_back(source.substr(start, offset - start));
            start = index;
        } else update_blocks(token, blocks);
    }
    result.push_back(source.substr(start));
    return result;
}

/* ── Parser state ─────────────────────────────────────────────────── */

struct parser_s {
    std::string_view source;
    size_t index = 0;
    specificity_s specificity;
    selector_dependency_t dependencies = SELECTOR_DEP_NONE;
    aether_pseudo_element_t pseudo_element = aether_pseudo_element_t::none;

    bool eof() const { return index >= source.size(); }
    char peek() const { return eof() ? '\0' : source[index]; }

    void skip_comments() { ::skip_comments(source, index); }
    bool skip_trivia() { return ::skip_trivia(source, index); }

    std::string read_identifier() {
        std::string value;
        if (!consume_name(source, index, &value)) return {};
        return value;
    }

    bool peek_balanced_body(std::string_view& body) {
        size_t cursor = index + 1, offset = 0;
        const size_t start = cursor;
        std::vector<char> blocks{')'};
        char token = '\0';
        while (next_structure(source, cursor, offset, token)) {
            update_blocks(token, blocks);
            if (blocks.empty()) {
                body = source.substr(start, offset - start);
                index = cursor;
                return true;
            }
        }
        return false;
    }
};

/* ── Forward declarations ─────────────────────────────────────────── */

static bool try_parse_compound(parser_s& p, compound_selector_s& compound);
static bool try_parse_attribute(parser_s& p, attribute_selector_s& attr);
static bool try_parse_pseudo(parser_s& p, pseudo_selector_s& pseudo,
                             specificity_s& spec, selector_dependency_t& deps);
static bool try_parse_nth(std::string_view source, nth_expression_s& expr);
static bool try_map_state(std::string_view name,
                          style_pseudo_state_t& required, style_pseudo_state_t& forbidden);
enum class selector_list_policy_t { ForgivingNonEmpty, ForgivingAllowEmpty, StrictNonEmpty };

static bool selector_parse_list_view(std::string_view source, std::vector<compiled_selector_s>& out,
                                     selector_list_policy_t policy);

/* ── Attribute operator reader ────────────────────────────────────── */

static bool try_read_attribute_operator(parser_s& p, attribute_match_operator_t& op) {
    if (p.eof()) return false;
    if (p.peek() == '=') { ++p.index; op = ATTR_EQUALS; return true; }
    if (p.index + 1 >= p.source.size() || p.source[p.index + 1] != '=') return false;
    switch (p.source[p.index]) {
        case '~': op = ATTR_INCLUDES;   break;
        case '|': op = ATTR_DASH_MATCH; break;
        case '^': op = ATTR_PREFIX;     break;
        case '$': op = ATTR_SUFFIX;     break;
        case '*': op = ATTR_CONTAINS;   break;
        default: return false;
    }
    p.index += 2;
    return true;
}

/* ── Attribute value reader ───────────────────────────────────────── */

static bool read_attribute_value(parser_s& p, std::string& value) {
    if (p.eof()) return false;
    if (p.peek() == '\'' || p.peek() == '"')
        return consume_string(p.source, p.index, &value);
    return consume_name(p.source, p.index, &value);
}

/* ── Attribute selector parser ────────────────────────────────────── */

static bool try_parse_attribute(parser_s& p, attribute_selector_s& attr) {
    ++p.index; /* skip '[' */
    p.skip_trivia();
    auto name = p.read_identifier();
    if (name.empty()) return false;
    p.skip_trivia();
    if (p.peek() == ']') {
        ++p.index;
        attr.name = std::move(name);
        attr.op = ATTR_EXISTS;
        attr.has_value = false;
        attr.case_insensitive = false;
        return true;
    }
    attribute_match_operator_t op = ATTR_EQUALS;
    if (!try_read_attribute_operator(p, op)) return false;
    p.skip_trivia();
    std::string val;
    if (!read_attribute_value(p, val)) return false;
    p.skip_trivia();
    bool ci = false;
    if (!p.eof() && p.peek() != ']') {
        auto flag = p.read_identifier();
        if (flag.size() == 1 && (flag[0] == 'i' || flag[0] == 'I')) { ci = true; }
        else if (flag.size() == 1 && (flag[0] == 's' || flag[0] == 'S')) { /* ok */ }
        else return false;
        p.skip_trivia();
    }
    if (p.eof() || p.peek() != ']') return false;
    ++p.index;
    attr.name = std::move(name);
    attr.op = op;
    attr.value = std::move(val);
    attr.has_value = true;
    attr.case_insensitive = ci;
    return true;
}

/* ── State pseudo-class mapping ───────────────────────────────────── */

static bool try_map_state(std::string_view name,
                          style_pseudo_state_t& required, style_pseudo_state_t& forbidden) {
    required = PSEUDO_STATE_NONE;
    forbidden = PSEUDO_STATE_NONE;

    if      (name == "hover")       required = PSEUDO_STATE_HOVER;
    else if (name == "active")      required = PSEUDO_STATE_ACTIVE;
    else if (name == "focus")       required = PSEUDO_STATE_FOCUS;
    else if (name == "focus-within") required = PSEUDO_STATE_FOCUS_WITHIN;
    else if (name == "disabled")    required = PSEUDO_STATE_DISABLED;
    else if (name == "checked")     required = PSEUDO_STATE_CHECKED;
    else if (name == "invalid")     required = PSEUDO_STATE_INVALID;
    else if (name == "read-only")   required = PSEUDO_STATE_READ_ONLY;
    else if (name == "selected")    required = PSEUDO_STATE_SELECTED;
    else if (name == "expanded")    required = PSEUDO_STATE_EXPANDED;
    else if (name == "enabled")     forbidden = PSEUDO_STATE_DISABLED;
    else if (name == "read-write")  forbidden = PSEUDO_STATE_READ_ONLY;

    return required != PSEUDO_STATE_NONE || forbidden != PSEUDO_STATE_NONE;
}

/* ── :nth-child expression parser ─────────────────────────────────── */

static bool try_parse_nth(std::string_view source, nth_expression_s& expr) {
    const auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    size_t cursor = 0;
    skip_trivia(source, cursor);
    source.remove_prefix(cursor);
    cursor = 0;
    const auto equals_ascii = [&](std::string_view word) {
        if (source.size() < word.size()) return false;
        for (size_t i = 0; i < word.size(); ++i) {
            char c = source[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
            if (c != word[i]) return false;
        }
        size_t end = word.size();
        skip_trivia(source, end);
        return end == source.size();
    };
    if (equals_ascii("odd")) { expr = {2, 1}; return true; }
    if (equals_ascii("even")) { expr = {2, 0}; return true; }

    // Signs belong to the coefficient/scalar or offset, never to the digits.
    const auto read_digits = [&](bool negative, int& value) {
        const size_t start = cursor;
        const uint64_t limit = negative ? 2147483648ULL : 2147483647ULL;
        uint64_t magnitude = 0;
        while (cursor < source.size() && is_digit(source[cursor])) {
            const auto digit = static_cast<unsigned>(source[cursor] - '0');
            if (magnitude > (limit - digit) / 10) return false;
            magnitude = magnitude * 10 + digit;
            ++cursor;
        }
        if (cursor == start) return false;
        const int64_t signed_value = negative ? -static_cast<int64_t>(magnitude)
                                              : static_cast<int64_t>(magnitude);
        value = static_cast<int>(signed_value);
        return true;
    };
    bool negative = false;
    if (cursor < source.size() && (source[cursor] == '+' || source[cursor] == '-')) {
        negative = source[cursor++] == '-';
        // '+' may be a delimiter followed by ident(n), without real whitespace.
        // A comment cannot build a signed number (+/**/2) or ident(-/**/n).
        if (!negative) {
            const size_t after_sign = cursor;
            skip_comments(source, cursor);
            if (cursor != after_sign &&
                (cursor == source.size() || (source[cursor] != 'n' && source[cursor] != 'N')))
                return false;
        }
    }
    int coefficient = negative ? -1 : 1;
    bool has_digits = cursor < source.size() && is_digit(source[cursor]);
    if (has_digits && !read_digits(negative, coefficient)) return false;
    if (cursor == source.size() || (source[cursor] != 'n' && source[cursor] != 'N')) {
        // The numeric token is complete. Trivia may follow a scalar but must
        // never join separate tokens into a dimension (2/**/n) or number.
        skip_trivia(source, cursor);
        if (!has_digits || cursor != source.size()) return false;
        expr = {0, coefficient};
        return true;
    }
    ++cursor;
    skip_trivia(source, cursor);
    int offset = 0;
    if (cursor < source.size()) {
        if (source[cursor] != '+' && source[cursor] != '-') return false;
        negative = source[cursor++] == '-';
        skip_trivia(source, cursor);
        if (!read_digits(negative, offset)) return false;
    }
    skip_trivia(source, cursor);
    if (cursor != source.size()) return false;
    expr = {coefficient, offset};
    return true;
}

/* ── Pseudo selector parser ───────────────────────────────────────── */

static bool try_parse_pseudo(parser_s& p, pseudo_selector_s& pseudo,
                             specificity_s& spec, selector_dependency_t& deps) {
    // The caller consumed the colon and any comments, and handled a second colon.
    // Pseudo names are ASCII-insensitive after CSS escape decoding, not their arguments.
    auto name = to_lower_ascii(p.read_identifier());
    if (name.empty()) return false;

    std::string_view body;
    bool has_body = false;
    if (!p.eof() && p.peek() == '(') {
        if (!p.peek_balanced_body(body)) return false;
        has_body = true;
    }

    /* Try state pseudo-classes */
    style_pseudo_state_t required, forbidden;
    if (try_map_state(name, required, forbidden)) {
        if (has_body) return false;
        if (required != PSEUDO_STATE_NONE) {
            pseudo.kind  = PSEUDO_REQUIRED_STATE;
            pseudo.state = required;
        } else {
            pseudo.kind  = PSEUDO_FORBIDDEN_STATE;
            pseudo.state = forbidden;
        }
        spec = spec + specificity_s{0, 1, 0};
        deps = deps | SELECTOR_DEP_SELF_STATE;
        return true;
    }

    if (!has_body) {
        pseudo_selector_kind_t kind;
        selector_dependency_t dep = SELECTOR_DEP_NONE;
        if      (name == "root")        { kind = PSEUDO_ROOT; }
        else if (name == "empty")       { kind = PSEUDO_EMPTY; dep = SELECTOR_DEP_STRUCTURE; }
        else if (name == "first-child") { kind = PSEUDO_FIRST_CHILD; dep = SELECTOR_DEP_STRUCTURE; }
        else if (name == "last-child")  { kind = PSEUDO_LAST_CHILD; dep = SELECTOR_DEP_STRUCTURE; }
        else if (name == "only-child")  { kind = PSEUDO_ONLY_CHILD; dep = SELECTOR_DEP_STRUCTURE; }
        else { return false; }
        pseudo.kind  = kind;
        pseudo.state = PSEUDO_STATE_NONE;
        spec = spec + specificity_s{0, 1, 0};
        deps = deps | dep;
        return true;
    }

    /* :nth-child(an+b) */
    if (name == "nth-child") {
        nth_expression_s nth;
        if (!try_parse_nth(body, nth)) return false;
        pseudo.kind = PSEUDO_NTH_CHILD;
        pseudo.nth  = nth;
        spec = spec + specificity_s{0, 1, 0};
        deps = deps | SELECTOR_DEP_STRUCTURE;
        return true;
    }

    /* :is() :where() :not() :has() */
    if (name == "is" || name == "where" || name == "not" || name == "has") {
        std::vector<compiled_selector_s> nested;
        // not/has require a nonempty, entirely valid list. is/where recover
        // locally and retain a present-empty argument when no branch survives.
        const auto policy = (name == "not" || name == "has")
            ? selector_list_policy_t::StrictNonEmpty
            : selector_list_policy_t::ForgivingAllowEmpty;
        if (!selector_parse_list_view(body, nested, policy)) return false;
        pseudo_selector_kind_t kind;
        if      (name == "is")   kind = PSEUDO_IS;
        else if (name == "where") kind = PSEUDO_WHERE;
        else if (name == "not")  kind = PSEUDO_NOT;
        else                     kind = PSEUDO_HAS;
        pseudo.kind   = kind;
        pseudo.nested = std::make_unique<std::vector<compiled_selector_s>>(std::move(nested));
        if (kind == PSEUDO_WHERE) {
            spec = spec + specificity_s{0, 0, 0};
        } else {
            specificity_s max_spec{};
            for (auto& s : *pseudo.nested)
                if (s.specificity.compare_to(max_spec) > 0) max_spec = s.specificity;
            spec = spec + max_spec;
        }
        selector_dependency_t nested_deps = SELECTOR_DEP_NONE;
        for (auto& s : *pseudo.nested)
            nested_deps = nested_deps | s.dependencies;
        deps = deps | nested_deps;
        if (kind == PSEUDO_HAS) deps = deps | SELECTOR_DEP_DESCENDANT;
        return true;
    }

    return false;
}

/* ── Compound selector parser ─────────────────────────────────────── */

static bool try_parse_compound(parser_s& p, compound_selector_s& compound) {
    bool consumed = false;

    if (!p.eof() && p.peek() == '*') {
        ++p.index;
        consumed = true;
    } else if (!p.eof() && (is_identifier_start(p.peek()) || p.peek() == '\\')) {
        compound.type_name = to_lower_ascii(p.read_identifier());
        if (compound.type_name.empty()) return false;
        p.specificity = p.specificity + specificity_s{0, 0, 1};
        consumed = true;
    }

    while (!p.eof()) {
        p.skip_comments();
        char c = p.peek();
        if (c == '#') {
            ++p.index;
            auto val = p.read_identifier();
            if (val.empty() || !compound.id.empty()) return false;
            compound.id = std::move(val);
            p.specificity = p.specificity + specificity_s{1, 0, 0};
            p.dependencies = p.dependencies | SELECTOR_DEP_SELF_ID;
            consumed = true;
        } else if (c == '.') {
            ++p.index;
            p.skip_comments();
            auto val = p.read_identifier();
            if (val.empty()) return false;
            compound.classes.push_back(std::move(val));
            p.specificity = p.specificity + specificity_s{0, 1, 0};
            p.dependencies = p.dependencies | SELECTOR_DEP_SELF_CLASS;
            consumed = true;
        } else if (c == '[') {
            attribute_selector_s attr;
            if (!try_parse_attribute(p, attr)) return false;
            compound.attributes.push_back(std::move(attr));
            p.specificity = p.specificity + specificity_s{0, 1, 0};
            p.dependencies = p.dependencies | SELECTOR_DEP_SELF_ATTRIBUTE;
            consumed = true;
        } else if (c == ':') {
            ++p.index;
            p.skip_comments();
            if (p.peek() == ':') {
                if (p.pseudo_element != aether_pseudo_element_t::none) return false;
                ++p.index;
                p.skip_comments();
                auto name = to_lower_ascii(p.read_identifier());
                if (name == "before") {
                    p.pseudo_element = aether_pseudo_element_t::before;
                } else if (name == "after") {
                    p.pseudo_element = aether_pseudo_element_t::after;
                } else {
                    return false;
                }
                // Pseudo-elements have type-selector specificity.
                p.specificity = p.specificity + specificity_s{0, 0, 1};
                consumed = true;
                continue;
            }
            pseudo_selector_s pseudo;
            specificity_s pseudo_spec{};
            selector_dependency_t pseudo_deps = SELECTOR_DEP_NONE;
            if (!try_parse_pseudo(p, pseudo, pseudo_spec, pseudo_deps)) return false;
            compound.pseudos.push_back(std::move(pseudo));
            p.specificity = p.specificity + pseudo_spec;
            p.dependencies = p.dependencies | pseudo_deps;
            consumed = true;
        } else {
            break;
        }
    }

    return consumed;
}

/* ── Selector parser (matches C# Parser.TryParse) ────────────────── */

static bool selector_parse_core(parser_s& p, compiled_selector_s& out) {
    std::vector<compound_selector_s> compounds;
    std::vector<selector_combinator_t> combinators;

    p.skip_trivia();
    while (!p.eof()) {
        compound_selector_s compound;
        if (!try_parse_compound(p, compound)) return false;
        compounds.push_back(std::move(compound));

        bool had_ws = p.skip_trivia();
        // ::before and ::after terminate a selector. They represent a generated child of the
        // element matched by the final compound, not an intermediate selector step.
        if (p.pseudo_element != aether_pseudo_element_t::none && !p.eof()) return false;
        if (p.eof()) break;

        selector_combinator_t comb;
        char c = p.peek();
        if      (c == '>') comb = COMB_CHILD;
        else if (c == '+') comb = COMB_ADJACENT_SIBLING;
        else if (c == '~') comb = COMB_GENERAL_SIBLING;
        else if (had_ws)   comb = COMB_DESCENDANT;
        else return false;

        if (c == '>' || c == '+' || c == '~') {
            ++p.index;
            p.skip_trivia();
        }
        if (p.eof()) return false;

        combinators.push_back(comb);
        if (comb == COMB_DESCENDANT || comb == COMB_CHILD)
            p.dependencies = p.dependencies | SELECTOR_DEP_ANCESTOR;
        else if (comb == COMB_ADJACENT_SIBLING || comb == COMB_GENERAL_SIBLING)
            p.dependencies = p.dependencies | SELECTOR_DEP_SIBLING;
    }

    if (compounds.empty()) return false;

    /* Build steps right-to-left (matches C# reversed iteration) */
    out.steps.reserve(compounds.size());
    for (int i = static_cast<int>(compounds.size()) - 1; i >= 0; --i) {
        selector_step_s step;
        step.compound = std::move(compounds[i]);
        step.relation_to_next = (i > 0) ? combinators[i - 1] : COMB_NONE;
        out.steps.push_back(std::move(step));
    }

    /* Anchor from rightmost compound (index 0 after reversal = original last) */
    auto& rightmost = out.steps[0].compound;
    out.anchor.id        = rightmost.id;
    out.anchor.class_name = rightmost.classes.empty() ? std::string() : rightmost.classes[0];
    out.anchor.type_name  = rightmost.type_name;

    out.specificity   = p.specificity;
    out.dependencies  = p.dependencies;
    out.pseudo_element = p.pseudo_element;
    return true;
}

/* ── Parse list (comma-separated, for nested :is/:where/:not/:has) ─ */

static bool selector_parse_list_view(std::string_view source, std::vector<compiled_selector_s>& out,
                                     selector_list_policy_t policy) {
    auto parts = split_top_level(source, ',');
    std::vector<compiled_selector_s> parsed;
    for (auto& part : parts) {
        compiled_selector_s sel;
        parser_s sub{part, 0, {}, SELECTOR_DEP_NONE};
        const bool valid = !part.empty() && selector_parse_core(sub, sel);
        sub.skip_trivia();
        if (!valid || !sub.eof()) {
            if (policy == selector_list_policy_t::StrictNonEmpty) return false;
            continue;
        }
        parsed.push_back(std::move(sel));
    }
    if (parsed.empty() && policy != selector_list_policy_t::ForgivingAllowEmpty) return false;
    // Publish only after the complete list succeeds; preserve the caller's prefix.
    for (auto& sel : parsed) out.push_back(std::move(sel));
    return true;
}

/* ── Public API ───────────────────────────────────────────────────── */

bool selector_parse(const char* source, compiled_selector_s& out) {
    if (!source) return false;
    std::string_view sv(source);
    if (sv.empty()) return false;
    parser_s p{sv, 0, {}, SELECTOR_DEP_NONE};
    if (!selector_parse_core(p, out)) return false;
    out.source = std::string(sv);
    return true;
}

bool selector_parse_list(const char* source, std::vector<compiled_selector_s>& out) {
    if (!source) return false;
    std::string_view sv(source);
    if (sv.empty()) return false;
    return selector_parse_list_view(sv, out, selector_list_policy_t::ForgivingNonEmpty);
}
