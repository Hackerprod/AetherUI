#include "style_runtime_types.h"

// ═══ Media condition AST — factory functions ═══════════════════
//
// Optimizing constructors that constant-fold:
//   And(Always, X) = X     Or(Never, X) = X
//   And(Never, X)  = Never  Or(Always, X) = Always
//
// All composite nodes are heap-allocated. The three static constants
// (Always / Never / Unknown) are never freed.

media_condition_s* media_condition_and(media_condition_s* left, media_condition_s* right) {
    if (left  == media_condition_always()) return right;
    if (right == media_condition_always()) return left;
    if (left  == media_condition_never() || right == media_condition_never()) {
        return media_condition_never();
    }
    auto* node = new media_condition_s();
    node->kind  = media_condition_s::kind_t::and_;
    node->left  = left;
    node->right = right;
    return node;
}

media_condition_s* media_condition_or(media_condition_s* left, media_condition_s* right) {
    if (left  == media_condition_never())  return right;
    if (right == media_condition_never())  return left;
    if (left  == media_condition_always() || right == media_condition_always()) {
        return media_condition_always();
    }
    auto* node = new media_condition_s();
    node->kind  = media_condition_s::kind_t::or_;
    node->left  = left;
    node->right = right;
    return node;
}

media_condition_s* media_condition_not(media_condition_s* value) {
    auto* node = new media_condition_s();
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

// Split on the "and" keyword at bracket depth 0.
static std::vector<std::string> split_and(const std::string& source) {
    std::vector<std::string> result;
    int32_t start = 0;
    int32_t depth = 0;
    char quote = '\0';
    int32_t len = static_cast<int32_t>(source.size());

    for (int32_t i = 0; i < len; ++i) {
        char c = source[i];
        if (quote != '\0') {
            if (c == '\\' && i + 1 < len) { ++i; continue; }
            if (c == quote) quote = '\0';
            continue;
        }
        if (c == '\'' || c == '"') { quote = c; continue; }
        if (c == '(') { ++depth; continue; }
        if (c == ')') { depth = std::max(0, depth - 1); continue; }

        if (depth == 0 && i + 3 <= len) {
            bool left_ok  = (i == 0 || std::isspace(static_cast<unsigned char>(source[i - 1])));
            bool right_ok = (i + 3 == len || std::isspace(static_cast<unsigned char>(source[i + 3])));
            if (left_ok && right_ok && str_iequals(source.substr(i, 3), "and")) {
                std::string part = str_trim(source.substr(start, i - start));
                if (!part.empty()) result.push_back(part);
                i += 2; // skip "and", loop increments
                start = i + 1;
            }
        }
    }

    std::string tail = str_trim(source.substr(start));
    if (!tail.empty()) result.push_back(tail);
    return result;
}

static media_condition_s* parse_term(
    const std::string& source,
    aether_style_diagnostic_fn diag,
    void* diag_data,
    int32_t offset)
{
    std::string trimmed = str_trim(source);

    if (str_iequals(trimmed, "all") || str_iequals(trimmed, "screen")) {
        return media_condition_always();
    }
    if (str_iequals(trimmed, "print")) {
        return media_condition_never();
    }

    if (trimmed.empty() || trimmed[0] != '(' || trimmed.back() != ')') {
        emit_diag(diag, diag_data, "AUIMED001", trimmed.c_str(), "",
                  ("Unsupported media term '" + trimmed + "'.").c_str());
        return media_condition_never();
    }

    std::string body = str_trim(trimmed.substr(1, trimmed.size() - 2));
    int32_t colon = find_top_level(body, ':');
    if (colon < 0) {
        emit_diag(diag, diag_data, "AUIMED002", body.c_str(), "",
                  ("Media feature '" + body + "' requires a value.").c_str());
        return media_condition_never();
    }

    std::string name  = str_to_lower(str_trim(body.substr(0, colon)));
    std::string value = str_trim(body.substr(colon + 1));

    // Length features
    if (name == "min-width" || name == "max-width" || name == "width" ||
        name == "min-height" || name == "max-height" || name == "height")
    {
        aether_css_length length_val = {};
        if (!aether_css_length_try_parse(value.c_str(), &length_val)) {
            emit_diag(diag, diag_data, "AUIMED003", name.c_str(), value.c_str(),
                      ("Invalid media length '" + value + "'.").c_str());
            return media_condition_never();
        }
        // Inspect the accepted source value: calc simplification can erase a
        // percentage coefficient through cancellation or multiplication by zero.
        // The shared length parser and ordinary CSS properties still allow %.
        if (value.find('%') != std::string::npos) {
            emit_diag(diag, diag_data, "AUIMED003", name.c_str(), value.c_str(),
                      ("Invalid media length '" + value + "'.").c_str());
            return media_condition_unknown();
        }
        auto* node = new media_condition_s();
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
        auto* node = new media_condition_s();
        node->kind = media_condition_s::kind_t::enum_feature;
        node->enum_feature = media_condition_s::enum_feature_t::color_scheme;
        std::string lv = str_to_lower(value);
        if (lv == "dark")        node->enum_value = 1;  // StyleColorScheme.Dark
        else if (lv == "light")  node->enum_value = 0;  // StyleColorScheme.Light
        else {
            emit_diag(diag, diag_data, "AUIMED005", name.c_str(), value.c_str(),
                      ("Invalid value '" + value + "' for media feature '" + name + "'.").c_str());
            delete node;
            return media_condition_never();
        }
        return node;
    }

    // pointer
    if (name == "pointer") {
        auto* node = new media_condition_s();
        node->kind = media_condition_s::kind_t::enum_feature;
        node->enum_feature = media_condition_s::enum_feature_t::pointer_accuracy;
        std::string lv = str_to_lower(value);
        if (lv == "none")   node->enum_value = 0;  // StylePointerAccuracy.None
        else if (lv == "coarse") node->enum_value = 1;
        else if (lv == "fine")   node->enum_value = 2;
        else {
            emit_diag(diag, diag_data, "AUIMED005", name.c_str(), value.c_str(),
                      ("Invalid value '" + value + "' for media feature '" + name + "'.").c_str());
            delete node;
            return media_condition_never();
        }
        return node;
    }

    // prefers-reduced-motion
    if (name == "prefers-reduced-motion") {
        auto* node = new media_condition_s();
        node->kind = media_condition_s::kind_t::enum_feature;
        node->enum_feature = media_condition_s::enum_feature_t::motion_preference;
        std::string lv = str_to_lower(value);
        if (lv == "reduce")         node->enum_value = 1;  // StyleMotionPreference.Reduce
        else if (lv == "no-preference") node->enum_value = 0;
        else {
            emit_diag(diag, diag_data, "AUIMED005", name.c_str(), value.c_str(),
                      ("Invalid value '" + value + "' for media feature '" + name + "'.").c_str());
            delete node;
            return media_condition_never();
        }
        return node;
    }

    emit_diag(diag, diag_data, "AUIMED004", name.c_str(), "",
              ("Unsupported media feature '" + name + "'.").c_str());
    return media_condition_never();
}

static media_condition_s* parse_alternative(
    const std::string& source,
    aether_style_diagnostic_fn diag,
    void* diag_data,
    int32_t offset)
{
    std::string trimmed = str_trim(source);
    bool negate = false;

    if (str_starts_with_i(trimmed, "not ")) {
        negate = true;
        trimmed = str_trim(trimmed.substr(4));
    }
    if (str_starts_with_i(trimmed, "only ")) {
        trimmed = str_trim(trimmed.substr(5));
    }

    auto parts = split_and(trimmed);
    media_condition_s* result = media_condition_always();
    for (const auto& part : parts) {
        media_condition_s* term = parse_term(part, diag, diag_data, offset);
        result = media_condition_and(result, term);
    }

    return negate ? media_condition_not(result) : result;
}

media_condition_s* media_condition_parse(
    const char* source,
    aether_style_diagnostic_fn diag,
    void* diag_data,
    int32_t offset)
{
    if (!source) return media_condition_never();

    auto alternatives = split_top_level(source, ',');
    media_condition_s* result = media_condition_never();
    for (const auto& alt : alternatives) {
        media_condition_s* parsed = parse_alternative(alt, diag, diag_data, offset);
        result = media_condition_or(result, parsed);
    }
    return result;
}
