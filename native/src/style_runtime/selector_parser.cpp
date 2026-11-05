#include "style_runtime_types.h"
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <charconv>

/* ── Helpers ──────────────────────────────────────────────────────── */

static std::string to_lower_ascii(std::string_view s) {
    std::string result(s);
    for (auto& c : result)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

static std::string unescape_string(std::string_view source) {
    if (source.find('\\') == std::string_view::npos)
        return std::string(source);
    std::string result;
    result.reserve(source.size());
    bool escaped = false;
    for (char c : source) {
        if (escaped) { result += c; escaped = false; }
        else if (c == '\\') { escaped = true; }
        else { result += c; }
    }
    if (escaped) result += '\\';
    return result;
}

static bool is_identifier_start(char c) {
    auto u = static_cast<unsigned char>(c);
    return std::isalpha(u) || c == '_' || c == '-' || u >= 128;
}

static bool is_identifier_part(char c) {
    auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || c == '-' || c == '\\' || u >= 128;
}

/* ── Top-level comma splitter (matches TextSplitter.SplitTopLevel) ── */

static std::vector<std::string> split_top_level(std::string_view source, char sep) {
    std::vector<std::string> result;
    size_t start = 0;
    int parens = 0, brackets = 0, braces = 0;
    char quote = '\0';
    bool escaped = false;

    for (size_t i = 0; i < source.size(); ++i) {
        char c = source[i];
        if (quote != '\0') {
            if (escaped) { escaped = false; }
            else if (c == '\\') { escaped = true; }
            else if (c == quote) { quote = '\0'; }
            continue;
        }
        if (c == '\'' || c == '"') { quote = c; continue; }
        switch (c) {
            case '(': ++parens; break;
            case ')': parens = std::max(0, parens - 1); break;
            case '[': ++brackets; break;
            case ']': brackets = std::max(0, brackets - 1); break;
            case '{': ++braces; break;
            case '}': braces = std::max(0, braces - 1); break;
            default:
                if (c == sep && parens == 0 && brackets == 0 && braces == 0) {
                    auto sv = source.substr(start, i - start);
                    /* trim */
                    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
                    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
                    result.emplace_back(sv);
                    start = i + 1;
                }
                break;
        }
    }
    auto sv = source.substr(start);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
    result.emplace_back(sv);
    return result;
}

/* ── Trim helper ──────────────────────────────────────────────────── */

static void ltrim(std::string_view& sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
        sv.remove_prefix(1);
}

static void rtrim(std::string_view& sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
        sv.remove_suffix(1);
}

static void trim(std::string_view& sv) { ltrim(sv); rtrim(sv); }

/* ── Parser state ─────────────────────────────────────────────────── */

struct parser_s {
    std::string_view source;
    size_t index = 0;
    specificity_s specificity;
    selector_dependency_t dependencies = SELECTOR_DEP_NONE;
    aether_pseudo_element_t pseudo_element = aether_pseudo_element_t::none;

    bool eof() const { return index >= source.size(); }
    char peek() const { return eof() ? '\0' : source[index]; }
    char advance() { return source[index++]; }

    bool skip_whitespace() {
        size_t start = index;
        while (!eof() && std::isspace(static_cast<unsigned char>(source[index])))
            ++index;
        return index > start;
    }

    std::string read_identifier() {
        size_t start = index;
        while (!eof() && is_identifier_part(source[index]))
            ++index;
        return start == index ? std::string() : unescape_string(source.substr(start, index - start));
    }

    std::string_view peek_balanced_body(bool trim_body = true) {
        size_t saved = index;
        ++index; /* skip '(' */
        size_t body_start = index;
        int depth = 1;
        char quote = '\0';
        bool escaped = false;
        while (!eof()) {
            char c = source[index];
            if (quote != '\0') {
                if (escaped) { escaped = false; }
                else if (c == '\\') { escaped = true; }
                else if (c == quote) { quote = '\0'; }
            } else if (c == '\'' || c == '"') {
                quote = c;
            } else if (c == '(') {
                ++depth;
            } else if (c == ')' && --depth == 0) {
                auto body = source.substr(body_start, index - body_start);
                if (trim_body) trim(body);
                ++index; /* advance past ')' */
                return body;
            }
            ++index;
        }
        index = saved; /* restore on failure */
        return {};
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
static bool selector_parse_list_view(std::string_view source, std::vector<compiled_selector_s>& out);

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

static std::string read_attribute_value(parser_s& p) {
    if (p.eof()) return {};
    char c = p.peek();
    if (c == '\'' || c == '"') {
        ++p.index;
        size_t start = p.index;
        bool escaped = false;
        while (!p.eof()) {
            c = p.peek();
            if (escaped) { escaped = false; }
            else if (c == '\\') { escaped = true; }
            else if (c == static_cast<char>(p.source[start - 1])) { /* matching quote */
                auto val = unescape_string(p.source.substr(start, p.index - start));
                ++p.index;
                return val;
            }
            ++p.index;
        }
        return {};
    }
    size_t start = p.index;
    while (!p.eof() && !std::isspace(static_cast<unsigned char>(p.peek())) && p.peek() != ']')
        ++p.index;
    return start == p.index ? std::string() : unescape_string(p.source.substr(start, p.index - start));
}

/* ── Attribute selector parser ────────────────────────────────────── */

static bool try_parse_attribute(parser_s& p, attribute_selector_s& attr) {
    ++p.index; /* skip '[' */
    p.skip_whitespace();
    auto name = p.read_identifier();
    if (name.empty()) return false;
    p.skip_whitespace();
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
    p.skip_whitespace();
    bool is_quoted = (!p.eof() && (p.peek() == '\'' || p.peek() == '"'));
    auto val = read_attribute_value(p);
    if (val.empty() && !is_quoted) return false;
    p.skip_whitespace();
    bool ci = false;
    if (!p.eof() && p.peek() != ']') {
        auto flag = p.read_identifier();
        if (flag.size() == 1 && (flag[0] == 'i' || flag[0] == 'I')) { ci = true; }
        else if (flag.size() == 1 && (flag[0] == 's' || flag[0] == 'S')) { /* ok */ }
        else return false;
        p.skip_whitespace();
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
    const auto is_space = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
    };
    const auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    while (!source.empty() && is_space(source.front())) source.remove_prefix(1);
    while (!source.empty() && is_space(source.back())) source.remove_suffix(1);
    const auto equals_ascii = [&](std::string_view word) {
        if (source.size() != word.size()) return false;
        for (size_t i = 0; i < word.size(); ++i) {
            char c = source[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
            if (c != word[i]) return false;
        }
        return true;
    };
    if (equals_ascii("odd")) { expr = {2, 1}; return true; }
    if (equals_ascii("even")) { expr = {2, 0}; return true; }

    size_t cursor = 0;
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
    if (cursor < source.size() && (source[cursor] == '+' || source[cursor] == '-'))
        negative = source[cursor++] == '-';
    int coefficient = negative ? -1 : 1;
    bool has_digits = cursor < source.size() && is_digit(source[cursor]);
    if (has_digits && !read_digits(negative, coefficient)) return false;
    if (cursor == source.size()) {
        if (!has_digits) return false;
        expr = {0, coefficient};
        return true;
    }
    if (source[cursor] != 'n' && source[cursor] != 'N') return false;
    ++cursor;
    while (cursor < source.size() && is_space(source[cursor])) ++cursor;
    int offset = 0;
    if (cursor < source.size()) {
        if (source[cursor] != '+' && source[cursor] != '-') return false;
        negative = source[cursor++] == '-';
        while (cursor < source.size() && is_space(source[cursor])) ++cursor;
        if (!read_digits(negative, offset)) return false;
    }
    if (cursor != source.size()) return false;
    expr = {coefficient, offset};
    return true;
}

/* ── Pseudo selector parser ───────────────────────────────────────── */

static bool try_parse_pseudo(parser_s& p, pseudo_selector_s& pseudo,
                             specificity_s& spec, selector_dependency_t& deps) {
    ++p.index; /* skip first ':' */
    if (!p.eof() && p.peek() == ':') return false; /* pseudo-elements rejected */

    auto name = p.read_identifier();
    if (name.empty()) return false;

    std::string_view body;
    bool has_body = false;
    if (!p.eof() && p.peek() == '(') {
        body = p.peek_balanced_body(name != "nth-child");
        if (body.empty()) return false;
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
        if (!selector_parse_list_view(body, nested) || nested.empty()) return false;
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
    } else if (!p.eof() && is_identifier_start(p.peek())) {
        compound.type_name = to_lower_ascii(p.read_identifier());
        p.specificity = p.specificity + specificity_s{0, 0, 1};
        consumed = true;
    }

    while (!p.eof()) {
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
            if (p.index + 1 < p.source.size() && p.source[p.index + 1] == ':') {
                if (p.pseudo_element != aether_pseudo_element_t::none) return false;
                p.index += 2;
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

    p.skip_whitespace();
    while (!p.eof()) {
        compound_selector_s compound;
        if (!try_parse_compound(p, compound)) return false;
        compounds.push_back(std::move(compound));

        bool had_ws = p.skip_whitespace();
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
            p.skip_whitespace();
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

static bool selector_parse_list_view(std::string_view source, std::vector<compiled_selector_s>& out) {
    auto parts = split_top_level(source, ',');
    bool any = false;
    for (auto& part : parts) {
        if (part.empty()) continue;
        compiled_selector_s sel;
        parser_s sub{part, 0, {}, SELECTOR_DEP_NONE};
        if (selector_parse_core(sub, sel)) {
            out.push_back(std::move(sel));
            any = true;
        }
    }
    return any;
}

/* ── Public API ───────────────────────────────────────────────────── */

bool selector_parse(const char* source, compiled_selector_s& out) {
    if (!source) return false;
    std::string_view sv(source);
    ltrim(sv);
    if (sv.empty()) return false;
    parser_s p{sv, 0, {}, SELECTOR_DEP_NONE};
    if (!selector_parse_core(p, out)) return false;
    out.source = std::string(sv);
    return true;
}

bool selector_parse_list(const char* source, std::vector<compiled_selector_s>& out) {
    if (!source) return false;
    std::string_view sv(source);
    ltrim(sv);
    if (sv.empty()) return false;
    return selector_parse_list_view(sv, out);
}
