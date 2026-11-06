#include "style_runtime_types.h"
#include "media_condition_allocation_scope.h"
#include "css_component_tokens.h"
#include "../css/css_length_internal.h"
#include <string_view>

static media_condition_s* allocate_media_node(media_condition_allocation_scope_s* scope) {
    return scope ? scope->make_node() : new media_condition_s();
}

static void discard_unreturned_node(media_condition_s* node, media_condition_allocation_scope_s* scope) {
    if (!scope) delete node;
}

// ═══ Media condition AST — factory functions ═══════════════════
//
// Optimizing constructors that constant-fold:
//   And(Always, X) = X     Or(Never, X) = X
//   And(Never, X)  = Never  Or(Always, X) = Always
//
// All composite nodes are heap-allocated. The three static constants
// (Always / Never / Unknown) are never freed.

media_condition_s* media_condition_and(media_condition_s* left, media_condition_s* right,
    media_condition_allocation_scope_s* scope) {
    if (left  == media_condition_always()) return right;
    if (right == media_condition_always()) return left;
    if (left  == media_condition_never() || right == media_condition_never()) {
        return media_condition_never();
    }
    auto* node = allocate_media_node(scope);
    node->kind  = media_condition_s::kind_t::and_;
    node->left  = left;
    node->right = right;
    return node;
}

media_condition_s* media_condition_or(media_condition_s* left, media_condition_s* right,
    media_condition_allocation_scope_s* scope) {
    if (left  == media_condition_never())  return right;
    if (right == media_condition_never())  return left;
    if (left  == media_condition_always() || right == media_condition_always()) {
        return media_condition_always();
    }
    auto* node = allocate_media_node(scope);
    node->kind  = media_condition_s::kind_t::or_;
    node->left  = left;
    node->right = right;
    return node;
}

media_condition_s* media_condition_not(media_condition_s* value, media_condition_allocation_scope_s* scope) {
    auto* node = allocate_media_node(scope);
    node->kind  = media_condition_s::kind_t::not_;
    node->inner = value;
    return node;
}

// ═══ Media condition matching ══════════════════════════════════

namespace {
enum class media_match { false_, true_, unknown };

media_match match_and(media_match left, media_match right) {
    if (left == media_match::false_ || right == media_match::false_) return media_match::false_;
    if (left == media_match::unknown || right == media_match::unknown) return media_match::unknown;
    return media_match::true_;
}

media_match match_or(media_match left, media_match right) {
    if (left == media_match::true_ || right == media_match::true_) return media_match::true_;
    if (left == media_match::unknown || right == media_match::unknown) return media_match::unknown;
    return media_match::false_;
}

media_match match_not(media_match value) {
    if (value == media_match::unknown) return value;
    return value == media_match::true_ ? media_match::false_ : media_match::true_;
}

static bool media_feature_matches(const media_condition_s* node, const aether_style_environment_t* env) {
    switch (node->kind) {
    case media_condition_s::kind_t::length_feature: {
        bool is_width =
            node->length_feature == media_condition_s::length_feature_t::min_width ||
            node->length_feature == media_condition_s::length_feature_t::max_width ||
            node->length_feature == media_condition_s::length_feature_t::width_equals;
        float actual = is_width ? env->viewport_width : env->viewport_height;
        float expected = aether_css_length_resolve(
            node->length_value,
            actual,
            env->root_font_size,
            env->root_font_size,
            env->viewport_width,
            env->viewport_height);
        if (!std::isfinite(expected)) return false;
        switch (node->length_feature) {
        case media_condition_s::length_feature_t::min_width:
        case media_condition_s::length_feature_t::min_height:
            return actual >= expected;
        case media_condition_s::length_feature_t::max_width:
        case media_condition_s::length_feature_t::max_height:
            return actual <= expected;
        default:
            return std::fabs(actual - expected) < 0.01f;
        }
    }
    case media_condition_s::kind_t::enum_feature:
        switch (node->enum_feature) {
        case media_condition_s::enum_feature_t::color_scheme:
            return env->color_scheme == node->enum_value;
        case media_condition_s::enum_feature_t::pointer_accuracy:
            return env->pointer_accuracy == node->enum_value;
        case media_condition_s::enum_feature_t::motion_preference:
            return env->motion_preference == node->enum_value;
        default:
            return false;
        }
    default:
        return false;
    }
}

media_match media_condition_evaluate(const media_condition_s* node, const aether_style_environment_t* env) {
    if (!node) return media_match::true_;
    switch (node->kind) {
    case media_condition_s::kind_t::always_true:
        return media_match::true_;
    case media_condition_s::kind_t::always_false:
        return media_match::false_;
    case media_condition_s::kind_t::unknown:
        return media_match::unknown;
    case media_condition_s::kind_t::and_:
        return match_and(media_condition_evaluate(node->left, env),
                         media_condition_evaluate(node->right, env));
    case media_condition_s::kind_t::or_:
        return match_or(media_condition_evaluate(node->left, env),
                        media_condition_evaluate(node->right, env));
    case media_condition_s::kind_t::not_:
        return match_not(media_condition_evaluate(node->inner, env));
    default:
        return media_feature_matches(node, env) ? media_match::true_ : media_match::false_;
    }
}
} // namespace

bool media_condition_matches(const media_condition_s* node, const aether_style_environment_t* env) {
    return media_condition_evaluate(node, env) == media_match::true_;
}

// ═══ Media condition parser ════════════════════════════════════

static void emit_diag(aether_style_diagnostic_fn diag, void* data,
                      const char* code, const char* prop,
                      const char* val, const char* msg)
{
    if (diag) diag(code, prop, val, msg, data);
}

namespace {
using component_kind = css_variable_token_kind;

std::string media_trim(const std::string& source) {
    const auto start = source.find_first_not_of(" \t\n\r\f");
    if (start == std::string::npos) return {};
    return source.substr(start, source.find_last_not_of(" \t\n\r\f") - start + 1);
}

bool media_string_has_bad_newline(std::string_view token) {
    for (size_t i = 1; i < token.size();) {
        if (token[i] == '\\') css_component_skip_escape(token, i);
        else if (css_component_newline(token[i])) return true;
        else ++i;
    }
    return false;
}

// The variables lexer recognizes URL tokens but deliberately does not enforce
// bad-url rules. Validate them here without changing custom-property behavior.
// An unescaped '(' in an atomic non-string token can only be its URL opener.
size_t media_url_open(std::string_view token) {
    if (token.empty() || token[0] == '\'' || token[0] == '"') return std::string_view::npos;
    for (size_t i = 0; i < token.size();) {
        if (token[i] == '\\') css_component_skip_escape(token, i);
        else if (token[i] == '(') return i;
        else ++i;
    }
    return std::string_view::npos;
}
bool media_valid_url(std::string_view token, size_t opening) {
    size_t i = opening + 1;
    while (i < token.size() && css_component_space(token[i])) ++i;
    while (i < token.size()) {
        const unsigned char c = static_cast<unsigned char>(token[i]);
        if (c == ')') return i + 1 == token.size();
        if (css_component_space(static_cast<char>(c))) {
            while (i < token.size() && css_component_space(token[i])) ++i;
            return i == token.size() || (token[i] == ')' && i + 1 == token.size());
        }
        if (c == '\'' || c == '"' || c == '(' || c <= 8 || c == 11 ||
            (c >= 14 && c <= 31) || c == 127) return false;
        if (c == '\\') {
            if (i + 1 < token.size() && css_component_newline(token[i + 1])) return false;
            css_component_skip_escape(token, i);
        } else ++i;
    }
    return true; // CSS URL tokens, like component blocks, may end at EOF.
}

struct media_component {
    component_kind kind;
    char delimiter;
    size_t begin, end;
};
struct media_alternative {
    std::vector<media_component> terms;
    bool valid = true;
};
struct media_components {
    std::string source;
    std::vector<media_alternative> alternatives{1};
};
char media_closer(char opening) {
    return opening == '(' ? ')' : opening == '[' ? ']' : '}';
}

// Validate before any constant folding: invalid syntax must never be turned
// into true by a leading NOT. Root commas alone recover an alternative; commas
// inside an unfinished block remain inside it until CSS's implicit EOF close.
media_components media_scan(const char* input) {
    media_components result;
    result.source = input;
    const size_t original_size = result.source.size();
    css_component_lexer lexer(input, original_size);
    std::vector<char> blocks;
    for (;;) {
        auto token = lexer.next();
        if (token.kind == component_kind::end) break;
        auto& alternative = result.alternatives.back();
        const std::string_view raw(input + token.begin, token.end - token.begin);
        const bool comment = raw.size() >= 2 && raw.substr(0, 2) == "/*";
        if (comment) {
            std::fill(result.source.begin() + token.begin, result.source.begin() + token.end, ' ');
            continue; // An unclosed comment consumes EOF, not a bad-string.
        }
        if (token.kind == component_kind::whitespace) continue;
        const bool string = !raw.empty() && (raw[0] == '\'' || raw[0] == '"');
        const size_t url = string ? std::string_view::npos : media_url_open(raw);
        if ((token.kind == component_kind::other || token.kind == component_kind::error) &&
            url != std::string_view::npos) {
            if (!media_valid_url(raw, url)) alternative.valid = false;
            if (token.kind == component_kind::error && token.end == original_size)
                result.source += ')';
            token.kind = component_kind::other;
        } else if (token.kind == component_kind::error) {
            if (string && token.end == original_size && !media_string_has_bad_newline(raw)) {
                // EOF terminates a CSS string; newline produces a bad-string.
                result.source += raw.front();
            } else alternative.valid = false;
            token.kind = component_kind::other;
        }
        if (blocks.empty() && token.kind == component_kind::comma) {
            result.alternatives.emplace_back();
            continue;
        }
        if (token.kind == component_kind::close) {
            if (blocks.empty() || media_closer(blocks.back()) != token.delimiter) {
                alternative.valid = false;
                // A mismatched close is a token in the current block. It does
                // not pop another block and expose one of its commas as OR.
            } else {
                blocks.pop_back();
                if (blocks.empty()) alternative.terms.back().end = token.end;
            }
            continue;
        }
        if (blocks.empty())
            alternative.terms.push_back({token.kind, token.delimiter, token.begin, token.end});
        if (token.kind == component_kind::open || token.kind == component_kind::function)
            blocks.push_back(token.delimiter);
    }
    while (!blocks.empty()) {
        result.source += media_closer(blocks.back());
        blocks.pop_back();
    }
    if (!result.alternatives.back().terms.empty())
        result.alternatives.back().terms.back().end = result.source.size();
    return result;
}

bool media_length(const std::string& source, aether_css_length& output) {
    // Inspect the source before simplification can erase percentage evidence.
    // This is the explicit limit of our calc subset, not a CSS-wide % rule.
    if (source.find('%') != std::string::npos) return false;
    aether::css::calc_value_s value;
    if (!aether::css::try_parse_calc(source, value) &&
        !aether::css::try_parse_numeric_literal(source, value)) return false;
    if (value.kind == aether::css::calc_value_kind_e::number) {
        if (!std::isfinite(value.number) || value.number != 0.0) return false;
        output = aether_css_length_zero();
    } else output = value.length;
    return true;
}
} // namespace

static media_condition_s* parse_term(
    const std::string& source,
    aether_style_diagnostic_fn diag,
    void* diag_data,
    int32_t /*offset*/,
    media_condition_allocation_scope_s* scope)
{
    std::string trimmed = media_trim(source);

    if (str_iequals(trimmed, "all") || str_iequals(trimmed, "screen")) {
        return media_condition_always();
    }
    if (str_iequals(trimmed, "print")) {
        return media_condition_never();
    }

    if (trimmed.empty() || trimmed[0] != '(' || trimmed.back() != ')') {
        emit_diag(diag, diag_data, "AUIMED001", trimmed.c_str(), "",
                  ("Unsupported media term '" + trimmed + "'.").c_str());
        return media_condition_unknown();
    }

    std::string body = media_trim(trimmed.substr(1, trimmed.size() - 2));
    int32_t colon = find_top_level(body, ':');
    if (colon < 0) {
        emit_diag(diag, diag_data, "AUIMED002", body.c_str(), "",
                  ("Media feature '" + body + "' requires a value.").c_str());
        return media_condition_unknown();
    }

    std::string name  = str_to_lower(media_trim(body.substr(0, colon)));
    std::string value = media_trim(body.substr(colon + 1));

    // Length features
    if (name == "min-width" || name == "max-width" || name == "width" ||
        name == "min-height" || name == "max-height" || name == "height")
    {
        aether_css_length length_val = {};
        if (!media_length(value, length_val)) {
            emit_diag(diag, diag_data, "AUIMED003", name.c_str(), value.c_str(),
                      ("Invalid media length '" + value + "'.").c_str());
            return media_condition_unknown();
        }
        auto* node = allocate_media_node(scope);
        node->kind = media_condition_s::kind_t::length_feature;
        node->length_value = length_val;
        if (name == "min-width")      node->length_feature = media_condition_s::length_feature_t::min_width;
        else if (name == "max-width") node->length_feature = media_condition_s::length_feature_t::max_width;
        else if (name == "width")     node->length_feature = media_condition_s::length_feature_t::width_equals;
        else if (name == "min-height") node->length_feature = media_condition_s::length_feature_t::min_height;
        else if (name == "max-height") node->length_feature = media_condition_s::length_feature_t::max_height;
        else                          node->length_feature = media_condition_s::length_feature_t::height_equals;
        return node;
    }

    // prefers-color-scheme
    if (name == "prefers-color-scheme") {
        auto* node = allocate_media_node(scope);
        node->kind = media_condition_s::kind_t::enum_feature;
        node->enum_feature = media_condition_s::enum_feature_t::color_scheme;
        std::string lv = str_to_lower(value);
        if (lv == "dark")        node->enum_value = 1;  // StyleColorScheme.Dark
        else if (lv == "light")  node->enum_value = 0;  // StyleColorScheme.Light
        else {
            emit_diag(diag, diag_data, "AUIMED005", name.c_str(), value.c_str(),
                      ("Invalid value '" + value + "' for media feature '" + name + "'.").c_str());
            discard_unreturned_node(node, scope);
            return media_condition_unknown();
        }
        return node;
    }

    // pointer
    if (name == "pointer") {
        auto* node = allocate_media_node(scope);
        node->kind = media_condition_s::kind_t::enum_feature;
        node->enum_feature = media_condition_s::enum_feature_t::pointer_accuracy;
        std::string lv = str_to_lower(value);
        if (lv == "none")   node->enum_value = 0;  // StylePointerAccuracy.None
        else if (lv == "coarse") node->enum_value = 1;
        else if (lv == "fine")   node->enum_value = 2;
        else {
            emit_diag(diag, diag_data, "AUIMED005", name.c_str(), value.c_str(),
                      ("Invalid value '" + value + "' for media feature '" + name + "'.").c_str());
            discard_unreturned_node(node, scope);
            return media_condition_unknown();
        }
        return node;
    }

    // prefers-reduced-motion
    if (name == "prefers-reduced-motion") {
        auto* node = allocate_media_node(scope);
        node->kind = media_condition_s::kind_t::enum_feature;
        node->enum_feature = media_condition_s::enum_feature_t::motion_preference;
        std::string lv = str_to_lower(value);
        if (lv == "reduce")         node->enum_value = 1;  // StyleMotionPreference.Reduce
        else if (lv == "no-preference") node->enum_value = 0;
        else {
            emit_diag(diag, diag_data, "AUIMED005", name.c_str(), value.c_str(),
                      ("Invalid value '" + value + "' for media feature '" + name + "'.").c_str());
            discard_unreturned_node(node, scope);
            return media_condition_unknown();
        }
        return node;
    }

    emit_diag(diag, diag_data, "AUIMED004", name.c_str(), "",
              ("Unsupported media feature '" + name + "'.").c_str());
    return media_condition_unknown();
}

static media_condition_s* parse_alternative(
    const media_components& components,
    const media_alternative& alternative,
    aether_style_diagnostic_fn diag,
    void* diag_data,
    int32_t offset,
    media_condition_allocation_scope_s* scope)
{
    auto text = [&](size_t i) {
        const auto& term = alternative.terms[i];
        return media_trim(components.source.substr(term.begin, term.end - term.begin));
    };
    auto keyword = [&](size_t i, const char* value) {
        return i < alternative.terms.size() && alternative.terms[i].kind == component_kind::ident &&
            str_iequals(text(i), value);
    };
    auto enclosed = [&](size_t i) {
        return alternative.terms[i].kind == component_kind::open && alternative.terms[i].delimiter == '(';
    };
    auto invalid = [&]() {
        emit_diag(diag, diag_data, "AUIMED006", "", "", "Invalid media query syntax.");
        return media_condition_never();
    };
    if (!alternative.valid || alternative.terms.empty()) return invalid();
    size_t first = 0;
    const bool negate = keyword(first, "not");
    const bool only = keyword(first, "only");
    if (negate || only) ++first;
    if (first == alternative.terms.size() || keyword(first, "not") || keyword(first, "only") ||
        keyword(first, "and") || keyword(first, "or")) return invalid();
    const bool type = alternative.terms[first].kind == component_kind::ident;
    if ((!type && !enclosed(first)) || (only && !type)) return invalid();
    for (size_t i = first + 1; i < alternative.terms.size(); i += 2)
        if (!keyword(i, "and") || i + 1 == alternative.terms.size() || !enclosed(i + 1)) return invalid();

    // Only after the whole alternative is known to be syntactically valid may
    // false/true siblings fold an unknown value, followed by the restrictor.
    media_condition_s* result;
    if (type) {
        const auto media_type = text(first);
        result = str_iequals(media_type, "all") || str_iequals(media_type, "screen")
            ? media_condition_always() : media_condition_never();
    } else result = parse_term(text(first), diag, diag_data, offset, scope);
    for (size_t i = first + 2; i < alternative.terms.size(); i += 2)
        result = media_condition_and(result, parse_term(text(i), diag, diag_data, offset, scope), scope);
    return negate ? media_condition_not(result, scope) : result;
}

media_condition_s* media_condition_parse(
    const char* source,
    aether_style_diagnostic_fn diag,
    void* diag_data,
    int32_t offset,
    media_condition_allocation_scope_s* scope)
{
    if (!source) return media_condition_never();
    const auto components = media_scan(source);
    media_condition_s* result = media_condition_never();
    for (const auto& alternative : components.alternatives)
        result = media_condition_or(result, parse_alternative(components, alternative, diag, diag_data, offset, scope), scope);
    return result;
}
