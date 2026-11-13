#include "style_runtime_types.h"
#include "style_source_limits.h"
#include "style_source_block_index.h"
#include "media_condition_allocation_scope.h"
#include <type_traits>
#include "css_component_tokens.h"
#include "css_important.h"
#include "css_single_identifier.h"
#include "supports_condition.h"
#include "paused_keyframes.h"
#include "style_property_registry_internal.h"
#include <string_view>
#include <cctype>

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
    bool typography_discarded = false;
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

struct declaration_span {
    size_t begin = 0, end = 0, colon = std::string::npos;
    bool valid = true;
};
static std::vector<declaration_span> declaration_spans(const std::string& source) {
    // Delimit only source tokens. Punctuation decoded from a name is data,
    // never a colon/semicolon/brace to be interpreted a second time.
    css_component_lexer lexer(source.data(), source.size());
    std::vector<declaration_span> spans;
    std::vector<char> blocks;
    declaration_span current;
    for (;;) {
        const auto token = lexer.next();
        if (token.kind == css_variable_token_kind::end ||
            (blocks.empty() && token.delimiter == ';')) {
            current.end = token.begin;
            spans.push_back(current);
            if (token.kind == css_variable_token_kind::end) return spans;
            current = declaration_span{};
            current.begin = token.end;
            continue;
        }
        if (token.kind == css_variable_token_kind::error) current.valid = false;
        if (blocks.empty() && token.delimiter == ':' && current.colon == std::string::npos)
            current.colon = token.begin;
        if (token.kind == css_variable_token_kind::open || token.kind == css_variable_token_kind::function)
            blocks.push_back(token.delimiter == '(' ? ')' : token.delimiter == '[' ? ']' : '}');
        else if (token.kind == css_variable_token_kind::close) {
            if (blocks.empty() || blocks.back() != token.delimiter) current.valid = false;
            else blocks.pop_back();
        }
    }
}
static bool declaration_name(const std::string& source, const declaration_span& span, std::string& name) {
    css_variable_lexer lexer(source.data(), span.colon, span.begin);
    css_variable_token token;
    do { token = lexer.next(); }
    while (token.kind == css_variable_token_kind::whitespace || token.kind == css_variable_token_kind::comment);
    if (token.kind != css_variable_token_kind::ident) return false;
    const auto ident = token;
    do { token = lexer.next(); }
    while (token.kind == css_variable_token_kind::whitespace || token.kind == css_variable_token_kind::comment);
    if (token.kind != css_variable_token_kind::end) return false;
    css_variable_lexer decoder(source.data() + ident.begin, ident.end - ident.begin);
    return decoder.decode_identifier([](void* out, const char* bytes, size_t size) {
        static_cast<std::string*>(out)->append(bytes, size);
        return true;
    }, &name);
}

static bool is_calc_expression(const std::string& value) {
    const std::string trimmed = str_trim(value);
    return trimmed.size() >= 5 && str_iequals(trimmed.substr(0, 5), "calc(");
}

// ═══ Compile longhand ══════════════════════════════════════════════════

static bool try_compile_longhand(
    const std::string& name,
    const std::string& value,
    bool important,
    int32_t declaration_order,
    style_property_registry_s* registry,
    compiled_property_declaration_s& output,
    aether_style_diagnostic_fn diagnostics_callback,
    void* diagnostics_user_data)
{
    const auto* def = registry->try_get(name.c_str());
    if (!def) return false;

    std::string normalized = str_trim(value);
    // Decode only for CSS-wide classification. The registered display parser
    // consumes source tokens itself; feeding it decoded text would re-interpret
    // an escaped backslash (e.g. \5c 66lex) as a second CSS escape.
    std::string wide_keyword = normalized;
    if (name == "display") {
        std::string ident;
        if (css_single_identifier(value, ident)) wide_keyword = std::move(ident);
    }
    compiled_style_value_s compiled;
    if (name == "font-family" || name == "font-style" || name == "letter-spacing" ||
        name == "font-size" || name == "font-weight" || name == "line-height")
        compiled.typography_source = normalized;

    const auto css_wide = def->css_wide ? def->css_wide(wide_keyword) : property_css_wide_e::none;
    if (css_wide == property_css_wide_e::initial || str_iequals(wide_keyword, "initial") || str_iequals(wide_keyword, "revert")) {
        compiled.mode = compiled_value_mode::initial;
    } else if (css_wide == property_css_wide_e::inherit || str_iequals(wide_keyword, "inherit")) {
        compiled.mode = compiled_value_mode::inherit;
    } else if (css_wide == property_css_wide_e::unset || str_iequals(wide_keyword, "unset")) {
        compiled.mode = compiled_value_mode::unset;
    } else if (contains_variable(normalized)) {
        compiled.mode = compiled_value_mode::deferred;
        compiled.deferred_source = normalized;
    } else {
        std::string parsed;
        if (!def->parse(normalized.c_str(), parsed)) {
            if (diagnostics_callback) {
                const auto failure = def->parse_failure ? def->parse_failure(normalized.c_str()) : property_apply_result_s{};
                if (failure.diagnostic_code) {
                    diagnostics_callback(failure.diagnostic_code, def->name.c_str(), normalized.c_str(),
                        failure.diagnostic_message, diagnostics_user_data);
                } else if (is_calc_expression(normalized)) {
                    diagnostics_callback("AETHER-CSS-CALC", def->name.c_str(), normalized.c_str(),
                        "The calc() expression is syntactically or dimensionally invalid.", diagnostics_user_data);
                }
            }
            return false;
        }
        compiled.mode = compiled_value_mode::value;
        compiled.typed_value = parsed;
    }

    compiled_property_declaration_s decl;
    // This supported pair is one cascade slot, including deferred/CSS-wide modes.
    decl.property_id = def->name == "-webkit-backdrop-filter"
        ? registry->try_get("backdrop-filter")->id : def->id;
    decl.value = compiled;
    decl.important = important;
    decl.declaration_order = declaration_order;
    output = std::move(decl);
    return true;
}

// Expand and validate the entire declaration before publishing any sibling.
// This helper is shared with computed substitution; it never applies values.
bool style_source_compile_shorthand(
    const style_shorthand_definition_s& shorthand,
    const std::string& source,
    bool important,
    int32_t declaration_order,
    style_property_registry_s* registry,
    std::vector<compiled_property_declaration_s>& output,
    aether_style_diagnostic_fn diagnostics_callback,
    void* diagnostics_user_data)
{
    if (shorthand.target_ids.empty() ||
        shorthand.target_ids.size() != shorthand.target_names.size()) return false;
    style_shorthand_writer_s writer;
    if (!shorthand.expand(source.c_str(), writer) ||
        writer.items.size() != shorthand.target_ids.size()) return false;
    std::vector<compiled_property_declaration_s> staged;
    staged.reserve(writer.items.size());
    for (const auto& item : writer.items) {
        compiled_property_declaration_s compiled;
        if (!try_compile_longhand(item.name, item.value, important, declaration_order,
                                  registry, compiled, diagnostics_callback, diagnostics_user_data)) return false;
        bool expected = false;
        for (auto id : shorthand.target_ids) if (id == compiled.property_id) expected = true;
        if (!expected) return false;
        for (const auto& previous : staged)
            if (previous.property_id == compiled.property_id) return false;
        staged.push_back(std::move(compiled));
    }
    output.insert(output.end(), staged.begin(), staged.end());
    return true;
}

// ═══ Compile declarations ══════════════════════════════════════════════

static declaration_compilation_result compile_declarations(
    const std::string& source,
    const std::string& original,
    style_property_registry_s* registry,
    aether_style_diagnostic_fn diagnostics_callback,
    void* diagnostics_user_data)
{
    declaration_compilation_result result;
    int32_t declaration_order = 0;

    for (const auto& segment : declaration_spans(source)) {
        if (!segment.valid || segment.colon == std::string::npos) continue;
        std::string name;
        if (!declaration_name(source, segment, name)) continue;
        std::string value = source.substr(segment.colon + 1, segment.end - segment.colon - 1);
        const bool important = css_strip_important(value);
        if (name.empty()) continue;
        // Empty custom properties are valid and must not trigger var() fallback.

        if (name.size() > 2 && name[0] == '-' && name[1] == '-') {
            custom_property_declaration_s custom;
            custom.name = name;
            custom.source = value;
            std::string original_value = original.substr(segment.colon + 1, segment.end - segment.colon - 1);
            css_strip_important(original_value);
            if (original_value != value) custom.original_source = std::move(original_value);
            custom.important = important;
            custom.declaration_order = declaration_order++;
            result.custom.push_back(custom);
            continue;
        }

        if (value.empty()) continue;
        name = str_to_lower(name);
        if (name == "font" || name == "all" || name == "font-stretch" ||
            name == "font-feature-settings" || name == "font-variation-settings" ||
            name == "word-spacing" || name == "unicode-bidi") result.typography_discarded = true;
        if (name == "backdrop-filter" || name == "-webkit-backdrop-filter") {
            value = original.substr(segment.colon + 1, segment.end - segment.colon - 1);
            css_strip_important(value);
        }
        if (name == "content") {
            compiled_content_declaration_s content;
            content.source = value;
            content.important = important;
            content.declaration_order = declaration_order++;
            result.content.push_back(std::move(content));
            continue;
        }

        const auto* shorthand = registry->try_get_shorthand(name.c_str());
        if (shorthand) {
            const int32_t order = declaration_order++;
            if (contains_variable(value)) {
                // Syntax is checked before cascade participation, without resolving
                // variables or testing the eventual property-specific grammar.
                if (validate_pending_declaration_value(value) && !shorthand->target_ids.empty())
                    result.shorthands.push_back({name, value, important, order});
                continue;
            }
            style_source_compile_shorthand(*shorthand, value, important, order, registry,
                result.properties, diagnostics_callback, diagnostics_user_data);
            continue;
        }

        if (!registry->try_get(name.c_str())) {
            continue;
        }

        compiled_property_declaration_s compiled;
        if (try_compile_longhand(name, value, important, declaration_order++, registry, compiled,
                                 diagnostics_callback, diagnostics_user_data))
            result.properties.push_back(std::move(compiled));
        else if (name == "font-family" || name == "font-style" || name == "letter-spacing" ||
        name == "font-size" || name == "font-weight" || name == "line-height")
            result.typography_discarded = true;
    }

    return result;
}

namespace {
struct supports_parser_s {
    const std::string& source;
    style_property_registry_s* registry;
    std::vector<css_variable_token> tokens;
    std::vector<size_t> mate;
    bool valid=true, exhausted=false;
    struct result { bool syntax=false, truth=false, covered=false; };
    supports_parser_s(const std::string& s, size_t& budget):source(s),registry(style_property_registry_get_global()) {
        css_component_lexer lexer(s.data(),s.size());
        std::vector<size_t> stack;
        for(;;){
            auto t=lexer.next();
            if(t.kind==css_variable_token_kind::end)break;
            if(!budget){exhausted=true;valid=false;return;} --budget;
            if(t.kind==css_variable_token_kind::error){valid=false;return;}
            if(t.kind==css_variable_token_kind::whitespace||t.kind==css_variable_token_kind::comment)continue;
            const size_t n=tokens.size(); tokens.push_back(t);mate.push_back(n);
            if(t.kind==css_variable_token_kind::open||t.kind==css_variable_token_kind::function){
                if(stack.size()>=32){valid=false;return;}stack.push_back(n);
            }else if(t.kind==css_variable_token_kind::close){
                if(stack.empty()){valid=false;return;}auto j=stack.back();stack.pop_back();
                const char c=tokens[j].delimiter;
                if(t.delimiter!=(c=='('?')':c=='['?']':'}')){valid=false;return;}
                mate[j]=n;mate[n]=j;
            }
        }
        if(!stack.empty())valid=false;
    }
    bool word(size_t p,const char* expected)const {
        if(p>=tokens.size()||tokens[p].kind!=css_variable_token_kind::ident)return false;
        std::string id;return css_single_identifier(source.substr(tokens[p].begin,tokens[p].end-tokens[p].begin),id)&&id==expected;
    }
    result declaration(size_t a,size_t b) {
        // Only one name : complete-value. No semicolon, braces or annotation
        // remnants may be ignored by the tolerant declaration-list compiler.
        if(a+2>b||tokens[a].kind!=css_variable_token_kind::ident||tokens[a+1].delimiter!=':')return {};
        if(a+2==b)return {true,false,false};
        for(size_t i=a+2;i<b;++i){
            if(tokens[i].delimiter==';'||tokens[i].delimiter=='{'||tokens[i].delimiter=='}')return {};
        }
        std::string name;
        if(!css_single_identifier(source.substr(tokens[a].begin,tokens[a].end-tokens[a].begin),name))return {};
        std::string value=source.substr(tokens[a+2].begin,tokens[b-1].end-tokens[a+2].begin);
        const bool important=css_strip_important(value);
        // A terminal annotation was stripped above; any remaining ! is invalid.
        css_component_lexer vl(value.data(),value.size());
        for(;;){auto t=vl.next();if(t.kind==css_variable_token_kind::end)break;if(t.delimiter=='!')return {true,false,false};}
        std::string id;const bool single=css_single_identifier(value,id);
        bool covered=name=="display"&&single&&(id=="none"||id=="block"||id=="inline"||id=="inline-block"||id=="flex"||id=="table"||id=="table-row"||id=="table-cell"||id=="initial"||id=="inherit"||id=="unset");
        if(!registry)return {true,false,false};
        compiled_property_declaration_s discarded;
        // No mutation, style application, diagnostic callbacks or external order.
        bool accepted=(!contains_variable(value)||validate_pending_declaration_value(value)) &&
            try_compile_longhand(name,value,important,0,registry,discarded,nullptr,nullptr);
        if(!accepted){
            if(const auto* sh=registry->try_get_shorthand(name.c_str())){
                std::vector<compiled_property_declaration_s> tmp;
                accepted=style_source_compile_shorthand(*sh,value,important,0,registry,tmp,nullptr,nullptr);
            }
        }
        return {true,accepted,covered};
    }
    result atom(size_t& p,size_t end,size_t depth) {
        if(p>=end||depth>32)return {};
        auto t=tokens[p];const size_t close=mate[p];
        if(t.kind==css_variable_token_kind::function){
            if(close<=p||close>=end)return {};
            p=close+1;return {true,false,false};
        }
        if(t.kind!=css_variable_token_kind::open||t.delimiter!='('||close<=p||close>=end)return {};
        const size_t a=p+1,b=close;p=close+1;
        if(a==b)return {true,false,false};
        if(a+1<b&&tokens[a].kind==css_variable_token_kind::ident&&tokens[a+1].delimiter==':'){auto r=declaration(a,b);return r.syntax?r:result{true,false,false};}
        // A condition starts with not or supports-in-parens. Anything else
        // starting with an identifier is general-enclosed, CSS false.
        if(tokens[a].kind==css_variable_token_kind::ident&&!word(a,"not"))return {true,false,false};
        auto nested=condition(a,b,depth+1);
        return nested.syntax?nested:result{true,false,false};
    }
    result condition(size_t a,size_t b,size_t depth=0) {
        if(depth>32||a==b)return {};
        size_t p=a;
        if(word(p,"not")){++p;auto r=atom(p,b,depth);if(!r.syntax||p!=b)return {};r.truth=!r.truth;return r;}
        auto r=atom(p,b,depth);if(!r.syntax)return {};
        int op=0;
        while(p<b){
            int next=word(p,"and")?1:word(p,"or")?2:0;
            if(!next||(op&&next!=op))return {};
            op=next;++p;
            auto rhs=atom(p,b,depth);if(!rhs.syntax)return {};
            r.covered=r.covered&&rhs.covered; // audit before boolean folding
            r.truth=op==1?(r.truth&&rhs.truth):(r.truth||rhs.truth);
        }
        return r;
    }
};
}
supports_condition_evidence_s supports_condition_probe(const std::string& source,size_t& budget) {
    supports_parser_s parser(source,budget);
    supports_condition_evidence_s e;e.budget_exhausted=parser.exhausted;
    if(!parser.valid||source.find('\0')!=std::string::npos)return e;
    auto r=parser.condition(0,parser.tokens.size());
    e.syntax_valid=r.syntax;e.matches=r.syntax&&r.truth;e.profile_supported=r.syntax&&r.covered;
    e.consumed=source.size();return e;
}

// ═══ Compile ranges (iterative depth-first source order) ════════════════

static aether_status_t source_limit_error(aether_style_diagnostic_fn callback, void* user_data,
                                         const char* reason) {
    if (callback) callback("AETHER-CSS-LIMIT", "", "", reason, user_data);
    return AETHER_ERROR_INVALID_STATE;
}

static aether_status_t compile_range(
    const std::string& source,
    const std::string& original,
    int32_t start,
    int32_t end,
    media_condition_s* inherited_condition,
    int64_t sheet_handle,
    style_sheet_layer layer,
    style_property_registry_s* registry,
    int32_t& rule_id_counter,
    std::vector<compiled_rule_s>& out_rules,
    aether_style_diagnostic_fn diagnostics_callback,
    void* diagnostics_user_data,
    media_condition_allocation_scope_s& media_scope)
{
    struct frame_s {
        int32_t index;
        int32_t end;
        media_condition_s* condition;
        bool active;
    };
    style_source_block_index_s blocks(source);
    std::vector<frame_s> frames{{start, end, inherited_condition, true}};
    size_t supports_budget=65536;
    size_t keyframes_budget=65536;
    while (!frames.empty()) {
        auto& frame = frames.back();
        auto& index = frame.index;
        const auto end = frame.end;
        skip_whitespace(source, index, end);
        if (index >= end) {
            frames.pop_back();
            continue;
        }

        if (source[index] == '@') {
            int32_t at_offset = index;
            index++;
            css_component_lexer at_lexer(original.data(),static_cast<size_t>(end),static_cast<size_t>(index));
            const auto name_token=at_lexer.next();
            if(name_token.kind!=css_variable_token_kind::ident && name_token.kind!=css_variable_token_kind::function){index=end;continue;}
            // @media(...) has no whitespace between its at-keyword and prelude.
            // The component lexer groups the following '(' with the name; keep
            // that punctuation in the prelude rather than dropping the rule.
            const size_t name_end=name_token.end-(name_token.kind==css_variable_token_kind::function?1:0);
            std::string name;
            if(!css_single_identifier(original.substr(name_token.begin,name_end-name_token.begin),name)){index=end;continue;}
            index=static_cast<int32_t>(name_end);
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

            const auto* extent = blocks.find(delimiter);
            if (!extent || extent->next > end) return AETHER_ERROR_INVALID_STATE;
            std::string prelude = str_trim(source.substr(index, delimiter - index));
            const std::string supports_prelude=original.substr(index,delimiter-index);

            // Commit the parent's source position before push_back can move
            // frames. The block-index cursor still belongs to the child next.
            index = extent->next;
            if ((name == "media" || name == "supports") && frames.size() > 32)
                return source_limit_error(diagnostics_callback, diagnostics_user_data, "Conditional nesting exceeds 32.");
            if (name == "supports") {
                const auto evidence=supports_condition_probe(supports_prelude, supports_budget);
                if(evidence.budget_exhausted)return source_limit_error(diagnostics_callback,diagnostics_user_data,"Supports token budget exhausted.");
                frames.push_back({delimiter+1,extent->body_end,frame.condition,frame.active&&evidence.syntax_valid&&evidence.matches});
            } else if (name == "keyframes") {
                auto keyframes=std::make_shared<paused_keyframes_s>(paused_keyframes_parse(
                    supports_prelude,original.substr(delimiter+1,extent->body_end-delimiter-1), &keyframes_budget));
                if(keyframes->limit_exceeded)return source_limit_error(diagnostics_callback,diagnostics_user_data,"Keyframes limit exceeded.");
                if(frame.active && !keyframes->name.empty()) {
                    if(!style_rule_ids_fit(rule_id_counter,1))return source_limit_error(diagnostics_callback,diagnostics_user_data,"Keyframes rule ID exhausted.");
                    compiled_rule_s rule;rule.rule_id=rule_id_counter++;rule.source_order=rule.rule_id;
                    rule.sheet_handle=sheet_handle;rule.layer=layer;rule.condition=frame.condition;
                    rule.keyframes=std::move(keyframes);out_rules.push_back(std::move(rule));
                }
            } else if (name == "media") {
                media_condition_s* media = media_condition_parse(
                    prelude.c_str(), nullptr, nullptr, at_offset, &media_scope);
                media_condition_s* combined = media_condition_and(frame.condition, media, &media_scope);
                frames.push_back({delimiter + 1, extent->body_end, combined, frame.active});
            }
            continue;
        }

        int32_t opening = find_open_brace(source, index, end);
        if (opening < 0) {
            frames.pop_back();
            continue;
        }
        const auto* extent = blocks.find(opening);
        if (!extent || extent->next > end) return AETHER_ERROR_INVALID_STATE;

        // Trailing whitespace can belong to a CSS escape in the final name.
        std::string selector_text = original.substr(index, opening - index);
        std::string declaration_text =
            source.substr(opening + 1, extent->body_end - opening - 1);

        std::vector<compiled_selector_s> selectors;
        if (!selector_text.empty()) {
            selector_parse_list(selector_text.c_str(), selectors);
        }

        auto declarations = compile_declarations(declaration_text,
            original.substr(opening + 1, extent->body_end - opening - 1),
            registry, diagnostics_callback, diagnostics_user_data);

        if (frame.active && !selectors.empty() &&
            (!declarations.properties.empty() || !declarations.custom.empty() || !declarations.content.empty() ||
             !declarations.shorthands.empty() || declarations.typography_discarded))
        {
            // Reserve the whole selector group before incrementing any ID.
            if (!style_rule_ids_fit(rule_id_counter, selectors.size()))
                return source_limit_error(diagnostics_callback, diagnostics_user_data,
                    "The cumulative stylesheet rule ID capacity is exhausted.");
            int32_t source_order = rule_id_counter;
            for (auto& sel : selectors) {
                compiled_rule_s rule;
                rule.rule_id = rule_id_counter++;
                rule.sheet_handle = sheet_handle;
                rule.layer = layer;
                rule.selector = std::move(sel);
                rule.condition = frame.condition;
                rule.typography_discarded = declarations.typography_discarded;
                rule.declarations = declarations.properties;
                rule.custom_declarations = declarations.custom;
                rule.content_declarations = declarations.content;
                rule.shorthand_declarations = declarations.shorthands;
                rule.source_order = source_order;
                out_rules.push_back(std::move(rule));
            }
        }

        index = extent->next;
    }
    return AETHER_OK;
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
    if (!style_source_size_fits(std::strlen(source)))
        return source_limit_error(diagnostics_callback, diagnostics_user_data,
            "The stylesheet source exceeds the 32-bit byte limit.");
    if (!style_rule_ids_fit(out_rule_id_counter, 0))
        return source_limit_error(diagnostics_callback, diagnostics_user_data,
            "The next stylesheet rule ID is invalid.");

    auto* registry = style_property_registry_get_global();
    if (!registry) return AETHER_ERROR_INVALID_STATE;

    // Both views have identical byte offsets. Selectors and the focal backdrop
    // consumer retain original tokens; other consumers keep the legacy view.
    const std::string original(source);
    const std::string cleaned = text_splitter_remove_comments(original);
    if (cleaned.size() != original.size()) return AETHER_ERROR_INVALID_STATE;

    style_sheet_layer sheet_layer = static_cast<style_sheet_layer>(layer);
    media_condition_s* always = media_condition_always();

    media_condition_allocation_scope_s media_scope;
    std::vector<compiled_rule_s> staged;
    int32_t next_rule_id = out_rule_id_counter;
    const auto status = compile_range(
        cleaned,
        original,
        0,
        static_cast<int32_t>(cleaned.size()),
        always,
        sheet_handle,
        sheet_layer,
        registry,
        next_rule_id,
        staged,
        diagnostics_callback,
        diagnostics_user_data,
        media_scope);
    if (status != AETHER_OK) return status;

    // All potentially allocating preparation precedes publication. The arena
    // also owns nodes that folding discarded before producing any rule root.
    const auto reached = media_condition_allocation_scope_s::reachable_from(staged);
    if (staged.size() > out_rules.max_size() - out_rules.size())
        return source_limit_error(diagnostics_callback, diagnostics_user_data,
            "The compiled rule output capacity is exhausted.");
    static_assert(std::is_nothrow_move_constructible_v<compiled_rule_s>);
    out_rules.reserve(out_rules.size() + staged.size());
    for (auto& rule : staged) out_rules.push_back(std::move(rule));
    out_rule_id_counter = next_rule_id;
    media_scope.release_reachable(reached);

    return AETHER_OK;
}

// ═══ Public API: inline style compilation ══════════════════════════════

aether_status_t style_source_compile_inline(
    const char* source,
    std::vector<compiled_property_declaration_s>& out_properties,
    std::vector<custom_property_declaration_s>& out_custom,
    std::vector<compiled_shorthand_declaration_s>& out_shorthands,
    aether_style_diagnostic_fn diagnostics_callback,
    void* diagnostics_user_data, bool* typography_discarded)
{
    if (!source) return AETHER_ERROR_NULL_ARG;
    if (!style_source_size_fits(std::strlen(source)))
        return source_limit_error(diagnostics_callback, diagnostics_user_data,
            "The entire inline style was ignored because it exceeds the 32-bit byte limit.");

    auto* registry = style_property_registry_get_global();
    if (!registry) return AETHER_ERROR_INVALID_STATE;

    const std::string original(source);
    const std::string cleaned = text_splitter_remove_comments(original);
    auto result = compile_declarations(cleaned, original, registry, diagnostics_callback, diagnostics_user_data);

    if (typography_discarded) *typography_discarded = result.typography_discarded;
    out_properties = std::move(result.properties);
    out_custom = std::move(result.custom);
    out_shorthands = std::move(result.shorthands);

    return AETHER_OK;
}
