#include "style_runtime_types.h"
#include "css_component_tokens.h"
#include "style_property_registry_internal.h"
#include <string_view>
#include <cctype>
#include <cstring>

// ═══ Comment removal (port of TextSplitter.RemoveComments) ════════════

std::string text_splitter_remove_comments(const std::string& source) {
    std::string output = source;
    css_component_lexer lexer(source.data(), source.size());
    for (;;) {
        const auto token = lexer.next();
        if (token.kind == css_variable_token_kind::end) break;
        // Include an EOF comment (the lexer reports it as error for variables).
        // Atomic strings and URL tokens protect comment-looking bytes inside.
        if (token.begin + 1 < source.size() && source[token.begin] == '/' && source[token.begin + 1] == '*')
            for (size_t i = token.begin; i < token.end; ++i)
                if (output[i] != '\n') output[i] = ' ';
    }
    return output;
}

// ═══ Internal compilation result ═══════════════════════════════════════

struct declaration_compilation_result {
    std::vector<compiled_property_declaration_s> properties;
    std::vector<custom_property_declaration_s> custom;
    std::vector<compiled_content_declaration_s> content;
    std::vector<compiled_shorthand_declaration_s> shorthands;
};

// ═══ Internal helpers ═══════════════════════════════════════════════════

static void skip_whitespace(const std::string& source, int32_t& index, int32_t end) {
    while (index < end && std::isspace(static_cast<unsigned char>(source[index]))) {
        index++;
    }
}

static int32_t find_open_brace(const std::string& source, int32_t start, int32_t end) {
    css_component_lexer lexer(source.data(), static_cast<size_t>(end), static_cast<size_t>(start));
    std::vector<char> blocks;
    for (;;) {
        const auto token = lexer.next();
        if (token.kind == css_variable_token_kind::end) return -1;
        if (blocks.empty() && token.delimiter == '{') return static_cast<int32_t>(token.begin);
        if (token.kind == css_variable_token_kind::open || token.kind == css_variable_token_kind::function)
            blocks.push_back(token.delimiter);
        else if (token.kind == css_variable_token_kind::close && !blocks.empty()) {
            const char opening = blocks.back();
            const char closing = opening == '(' ? ')' : opening == '[' ? ']' : '}';
            if (token.delimiter == closing) blocks.pop_back();
        }
    }
}

static int32_t find_matching_brace(const std::string& source, int32_t opening, int32_t end) {
    css_component_lexer lexer(source.data(), static_cast<size_t>(end), static_cast<size_t>(opening + 1));
    int32_t depth = 1;
    for (;;) {
        const auto token = lexer.next();
        if (token.kind == css_variable_token_kind::end) return -1;
        if (token.delimiter == '{') ++depth;
        if (token.delimiter == '}' && --depth == 0) return static_cast<int32_t>(token.begin);
    }
}

static int32_t find_at_rule_delimiter(const std::string& source, int32_t start, int32_t end) {
    css_component_lexer lexer(source.data(), static_cast<size_t>(end), static_cast<size_t>(start));
    std::vector<char> blocks;
    for (;;) {
        const auto token = lexer.next();
        if (token.kind == css_variable_token_kind::end) return -1;
        // Use component tokens rather than raw characters: a bad-string ends
        // at newline, and even a bad-url is consumed as one atomic token.
        if (blocks.empty() && (token.delimiter == ';' || token.delimiter == '{'))
            return static_cast<int32_t>(token.begin);
        if (token.kind == css_variable_token_kind::open || token.kind == css_variable_token_kind::function)
            blocks.push_back(token.delimiter);
        else if (token.kind == css_variable_token_kind::close && !blocks.empty()) {
            const char opening = blocks.back();
            const char closing = opening == '(' ? ')' : opening == '[' ? ']' : '}';
            if (token.delimiter == closing) blocks.pop_back();
        }
    }
}

static bool contains_variable(const std::string& s) {
    return variable_has_reference(s);
}

static bool try_strip_important(std::string& value) {
    std::string trimmed = str_trim(value);
    const char marker[] = "!important";
    size_t marker_len = std::strlen(marker);
    if (trimmed.size() < marker_len) {
        value = trimmed;
        return false;
    }
    std::string tail = trimmed.substr(trimmed.size() - marker_len);
    if (!str_iequals(tail, marker)) {
        value = trimmed;
        return false;
    }
    size_t marker_start = trimmed.size() - marker_len;
    if (marker_start > 0 && trimmed[marker_start - 1] == '\\') {
        value = trimmed;
        return false;
    }
    value = str_trim(trimmed.substr(0, marker_start));
    return true;
}

static std::vector<std::string> split_and_trim(const std::string& s, char ch) {
    auto parts = split_top_level(s, ch);
    std::vector<std::string> result;
    result.reserve(parts.size());
    for (auto& p : parts) {
        result.push_back(str_trim(p));
    }
    return result;
}

static int32_t find_top_level_colon(const std::string& s) {
    return find_top_level(s, ':');
}

static bool is_calc_expression(const std::string& value) {
    const std::string trimmed = str_trim(value);
    return trimmed.size() >= 5 && str_iequals(trimmed.substr(0, 5), "calc(");
}

// ═══ Compile longhand ══════════════════════════════════════════════════

static void compile_longhand(
    const std::string& name,
    const std::string& value,
    bool important,
    int32_t declaration_order,
    style_property_registry_s* registry,
    std::vector<compiled_property_declaration_s>& output,
    aether_style_diagnostic_fn diagnostics_callback,
    void* diagnostics_user_data)
{
    const auto* def = registry->try_get(name.c_str());
    if (!def) return;

    std::string normalized = str_trim(value);
    compiled_style_value_s compiled;

    if (str_iequals(normalized, "initial") || str_iequals(normalized, "revert")) {
        compiled.mode = compiled_value_mode::initial;
    } else if (str_iequals(normalized, "inherit")) {
        compiled.mode = compiled_value_mode::inherit;
    } else if (str_iequals(normalized, "unset")) {
        compiled.mode = compiled_value_mode::unset;
    } else if (contains_variable(normalized)) {
        compiled.mode = compiled_value_mode::deferred;
        compiled.deferred_source = normalized;
    } else {
        std::string parsed;
        if (!def->parse(normalized.c_str(), parsed)) {
            if (diagnostics_callback && is_calc_expression(normalized)) {
                diagnostics_callback("AETHER-CSS-CALC", def->name.c_str(), normalized.c_str(),
                    "The calc() expression is syntactically or dimensionally invalid.", diagnostics_user_data);
            }
            return;
        }
        compiled.mode = compiled_value_mode::value;
        compiled.typed_value = parsed;
    }

    compiled_property_declaration_s decl;
    decl.property_id = def->id;
    decl.value = compiled;
    decl.important = important;
    decl.declaration_order = declaration_order;
    output.push_back(decl);
}

// ═══ Compile declarations ══════════════════════════════════════════════

static declaration_compilation_result compile_declarations(
    const std::string& source,
    style_property_registry_s* registry,
    aether_style_diagnostic_fn diagnostics_callback,
    void* diagnostics_user_data)
{
    declaration_compilation_result result;
    int32_t declaration_order = 0;

    auto segments = split_and_trim(source, ';');
    for (auto& segment : segments) {
        if (segment.empty()) continue;

        int32_t colon = find_top_level_colon(segment);
        if (colon <= 0) continue;

        std::string name = str_to_lower(str_trim(segment.substr(0, colon)));
        std::string value = str_trim(segment.substr(colon + 1));
        if (name.empty() || value.empty()) continue;

        bool important = try_strip_important(value);

        if (name == "content") {
            compiled_content_declaration_s content;
            content.source = value;
            content.important = important;
            content.declaration_order = declaration_order++;
            result.content.push_back(std::move(content));
            continue;
        }

        if (name.size() >= 2 && name[0] == '-' && name[1] == '-') {
            custom_property_declaration_s custom;
            custom.name = name;
            custom.source = value;
            custom.important = important;
            custom.declaration_order = declaration_order++;
            result.custom.push_back(custom);
            continue;
        }

        const auto* shorthand = registry->try_get_shorthand(name.c_str());
        if (shorthand) {
            if (contains_variable(value)) {
                // The shorthand value depends on custom properties that resolve per
                // node. Keep the raw declaration; resolution expands it against the
                // node's variable scope (see compute_node in style_runtime_core.cpp).
                result.shorthands.push_back({name, value, important, declaration_order++});
                continue;
            }

            style_shorthand_writer_s writer;
            if (!shorthand->expand(value.c_str(), writer)) {
                continue;
            }

            for (auto& item : writer.items) {
                compile_longhand(
                    item.name, item.value, important,
                    declaration_order++, registry, result.properties,
                    diagnostics_callback, diagnostics_user_data);
            }
            continue;
        }

        if (!registry->try_get(name.c_str())) {
            continue;
        }

        compile_longhand(
            name, value, important,
            declaration_order++, registry, result.properties,
            diagnostics_callback, diagnostics_user_data);
    }

    return result;
}

// ═══ Compile range (recursive) ═════════════════════════════════════════

static void compile_range(
    const std::string& source,
    int32_t start,
    int32_t end,
    media_condition_s* inherited_condition,
    int64_t sheet_handle,
    style_sheet_layer layer,
    style_property_registry_s* registry,
    int32_t& rule_id_counter,
    std::vector<compiled_rule_s>& out_rules,
    aether_style_diagnostic_fn diagnostics_callback,
    void* diagnostics_user_data)
{
    int32_t index = start;
    while (index < end) {
        skip_whitespace(source, index, end);
        if (index >= end) break;

        if (source[index] == '@') {
            int32_t at_offset = index;
            index++;
            int32_t name_start = index;
            while (index < end &&
                   (std::isalnum(static_cast<unsigned char>(source[index])) ||
                    source[index] == '-' || source[index] == '_')) {
                index++;
            }

            std::string name = str_to_lower(source.substr(name_start, index - name_start));
            skip_whitespace(source, index, end);

            int32_t delimiter = find_at_rule_delimiter(source, index, end);
            if (delimiter < 0) {
                index = end;
                continue;
            }

            if (source[delimiter] == ';') {
                index = delimiter + 1;
                continue;
            }

            int32_t closing = find_matching_brace(source, delimiter, end);
            if (closing < 0) {
                index = end;
                continue;
            }

            std::string prelude = str_trim(source.substr(index, delimiter - index));

            if (name == "media") {
                media_condition_s* media = media_condition_parse(
                    prelude.c_str(), nullptr, nullptr, static_cast<int32_t>(at_offset));
                media_condition_s* combined = media_condition_and(inherited_condition, media);
                compile_range(
                    source, delimiter + 1, closing, combined,
                    sheet_handle, layer, registry, rule_id_counter, out_rules,
                    diagnostics_callback, diagnostics_user_data);
            }

            index = closing + 1;
            continue;
        }

        int32_t opening = find_open_brace(source, index, end);
        if (opening < 0) {
            break;
        }

        int32_t closing = find_matching_brace(source, opening, end);
        if (closing < 0) {
            break;
        }

        std::string selector_text = str_trim(
            source.substr(index, opening - index));
        std::string declaration_text =
            source.substr(opening + 1, closing - opening - 1);

        std::vector<compiled_selector_s> selectors;
        if (!selector_text.empty()) {
            selector_parse_list(selector_text.c_str(), selectors);
        }

        auto declarations = compile_declarations(declaration_text, registry, diagnostics_callback, diagnostics_user_data);

        if (!selectors.empty() &&
            (!declarations.properties.empty() || !declarations.custom.empty() || !declarations.content.empty() ||
             !declarations.shorthands.empty()))
        {
            int32_t source_order = rule_id_counter;
            for (auto& sel : selectors) {
                compiled_rule_s rule;
                rule.rule_id = rule_id_counter++;
                rule.sheet_handle = sheet_handle;
                rule.layer = layer;
                rule.selector = std::move(sel);
                rule.condition = inherited_condition;
                rule.declarations = declarations.properties;
                rule.custom_declarations = declarations.custom;
                rule.content_declarations = declarations.content;
                rule.shorthand_declarations = declarations.shorthands;
                rule.source_order = source_order;
                out_rules.push_back(std::move(rule));
            }
        }

        index = closing + 1;
    }
}

// ═══ Public API: stylesheet compilation ════════════════════════════════

aether_status_t style_source_compile(
    const char* source,
    int64_t sheet_handle,
    int32_t layer,
    std::vector<compiled_rule_s>& out_rules,
    int32_t& out_rule_id_counter,
    aether_style_diagnostic_fn diagnostics_callback,
    void* diagnostics_user_data)
{
    if (!source) return AETHER_ERROR_NULL_ARG;

    auto* registry = style_property_registry_get_global();
    if (!registry) return AETHER_ERROR_INVALID_STATE;

    std::string cleaned = text_splitter_remove_comments(source);

    style_sheet_layer sheet_layer = static_cast<style_sheet_layer>(layer);
    media_condition_s* always = media_condition_always();

    compile_range(
        cleaned,
        0,
        static_cast<int32_t>(cleaned.size()),
        always,
        sheet_handle,
        sheet_layer,
        registry,
        out_rule_id_counter,
        out_rules,
        diagnostics_callback,
        diagnostics_user_data);

    return AETHER_OK;
}

// ═══ Public API: inline style compilation ══════════════════════════════

aether_status_t style_source_compile_inline(
    const char* source,
    std::vector<compiled_property_declaration_s>& out_properties,
    std::vector<custom_property_declaration_s>& out_custom,
    aether_style_diagnostic_fn diagnostics_callback,
    void* diagnostics_user_data)
{
    if (!source) return AETHER_ERROR_NULL_ARG;

    auto* registry = style_property_registry_get_global();
    if (!registry) return AETHER_ERROR_INVALID_STATE;

    std::string cleaned = text_splitter_remove_comments(source);
    auto result = compile_declarations(cleaned, registry, diagnostics_callback, diagnostics_user_data);

    out_properties = std::move(result.properties);
    out_custom = std::move(result.custom);

    return AETHER_OK;
}
