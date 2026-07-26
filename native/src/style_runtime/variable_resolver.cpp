#include "style_runtime_types.h"

// ═══ Custom property scope ═════════════════════════════════════
//
// A linked list of dictionaries. Each scope holds the custom property
// values defined at a particular level (element, stylesheet, etc.).
// Lookup walks the chain toward the root until the name is found.

bool custom_property_scope_s::try_get(const std::string& name, std::string& value) const {
    auto it = local.find(name);
    if (it != local.end()) {
        value = it->second;
        return true;
    }
    if (parent) {
        return parent->try_get(name, value);
    }
    value.clear();
    return false;
}

// ═══ Variable resolver ═════════════════════════════════════════
//
// Expands var(--name, fallback) references in a CSS value string.
// Walks the scope chain for each variable name. Uses a set to detect
// cycles (re-entrant variable references). Enforces max depth and
// max expansion length from runtime options.

static bool find_closing_paren(const std::string& source, int32_t start, int32_t& closing) {
    int32_t depth = 1;
    char quote = '\0';
    bool escaped = false;
    int32_t len = static_cast<int32_t>(source.size());
    for (int32_t i = start; i < len; ++i) {
        char c = source[i];
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == quote) {
                quote = '\0';
            }
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')' && --depth == 0) {
            closing = i;
            return true;
        }
    }
    closing = -1;
    return false;
}

static bool try_resolve_core(
    const std::string& source,
    const custom_property_scope_s* scope,
    std::unordered_set<std::string>& stack,
    int32_t depth,
    int32_t max_depth,
    int32_t max_expansion_length,
    std::string& out)
{
    if (depth > max_depth) {
        out.clear();
        return false;
    }

    std::string output;
    output.reserve(std::min(source.size() + 16,
        static_cast<size_t>(max_expansion_length)));

    int32_t index = 0;
    int32_t src_len = static_cast<int32_t>(source.size());

    while (index < src_len) {
        // Case-insensitive search for "var("
        int32_t var_start = -1;
        {
            int32_t search_from = index;
            while (search_from + 4 <= src_len) {
                if ((source[search_from]     == 'v' || source[search_from]     == 'V') &&
                    (source[search_from + 1] == 'a' || source[search_from + 1] == 'A') &&
                    (source[search_from + 2] == 'r' || source[search_from + 2] == 'R') &&
                     source[search_from + 3] == '(')
                {
                    var_start = search_from;
                    break;
                }
                ++search_from;
            }
        }

        if (var_start < 0) {
            output.append(source, index, src_len - index);
            break;
        }

        output.append(source, index, var_start - index);

        int32_t body_start = var_start + 4;
        int32_t closing = -1;
        if (!find_closing_paren(source, body_start, closing)) {
            out.clear();
            return false;
        }

        std::string body = source.substr(body_start, closing - body_start);
        int32_t comma = find_top_level(body, ',');
        std::string name = str_trim(comma < 0 ? body : body.substr(0, comma));
        std::string fallback;
        if (comma >= 0) {
            fallback = str_trim(body.substr(comma + 1));
        }

        if (!str_starts_with_i(name, "--") || name.size() < 3) {
            out.clear();
            return false;
        }

        std::string replacement;
        bool found_replacement = false;

        std::string raw;
        if (scope->try_get(name, raw) &&
            raw != custom_property_invalid_value() &&
            stack.insert(name).second)
        {
            std::string resolved;
            if (try_resolve_core(raw, scope, stack, depth + 1,
                                 max_depth, max_expansion_length, resolved))
            {
                replacement = std::move(resolved);
                found_replacement = true;
            }
            stack.erase(name);
        }

        if (!found_replacement && !fallback.empty()) {
            std::string resolved_fallback;
            if (try_resolve_core(fallback, scope, stack, depth + 1,
                                 max_depth, max_expansion_length, resolved_fallback))
            {
                replacement = std::move(resolved_fallback);
                found_replacement = true;
            }
        }

        if (!found_replacement) {
            out.clear();
            return false;
        }

        output.append(replacement);
        if (static_cast<int32_t>(output.size()) > max_expansion_length) {
            out.clear();
            return false;
        }

        index = closing + 1;
    }

    if (static_cast<int32_t>(output.size()) > max_expansion_length) {
        out.clear();
        return false;
    }

    out = std::move(output);
    return true;
}

bool variable_resolve(
    const char* source,
    const custom_property_scope_s* scope,
    int32_t max_depth,
    int32_t max_expansion_length,
    std::string& out_value)
{
    if (!source) {
        out_value.clear();
        return false;
    }
    std::unordered_set<std::string> stack;
    return try_resolve_core(source, scope, stack, 0, max_depth, max_expansion_length, out_value);
}
