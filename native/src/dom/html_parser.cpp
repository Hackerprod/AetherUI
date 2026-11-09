#include "dom_types.h"
#include "dom_style_source.h"
#include "dom_strings.h"
#include "../style_runtime/aether_dom_style_binding.h"
#include "html_attribute_name.h"
#include "../abi/aether_abi.h"
#include <cctype>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string_view>
#include <memory>

static bool is_ascii_alpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool is_name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) ||
           c == '-' || c == '_' || c == ':' || c == '.';
}

static bool is_void_element(const std::string& tag) {
    static const char* voids[] = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr"
    };
    for (auto* v : voids) {
        if (aether::ascii_iequals(tag, v)) return true;
    }
    return false;
}

static bool is_raw_text_end_boundary(char c) {
    // HTML whitespace, not locale-sensitive isspace (which also accepts VT).
    // Unlike a browser tokenizer, this byte parser has not normalized CR to LF.
    return c == '>' || c == '/' || aether::is_html_space(static_cast<unsigned char>(c));
}

/* ── Parser ───────────────────────────────────────────────────────── */

class HtmlParser {
    const char* src;
    size_t len;
    size_t pos;

    char current() const { return pos < len ? src[pos] : '\0'; }
    bool end() const { return pos >= len; }
    char peek(int off) const { return (pos + off) < len ? src[pos + off] : '\0'; }

    bool starts_with(const char* prefix) const {
        size_t plen = std::strlen(prefix);
        return (pos + plen <= len) && std::strncmp(src + pos, prefix, plen) == 0;
    }

    void skip_whitespace() {
        while (!end() && std::isspace(static_cast<unsigned char>(current())))
            pos++;
    }

    std::string read_name() {
        size_t start = pos;
        while (!end() && is_name_char(current()))
            pos++;
        return std::string(src + start, pos - start);
    }

    void skip_until_tag_end() {
        while (!end() && current() != '>')
            pos++;
        if (!end()) pos++;
    }

    std::string read_attribute_value() {
        if (end()) return {};

        if (current() == '\'' || current() == '"') {
            char quote = current();
            pos++;
            size_t start = pos;
            while (!end() && current() != quote)
                pos++;
            std::string val(src + start, pos - start);
            if (!end()) pos++;
            return val;
        }

        // In an unquoted value, '/' is data, including immediately before '>'.
        // The outer tag parser recognizes '/>' only outside an attribute value.
        size_t start = pos;
        while (!end() && !std::isspace(static_cast<unsigned char>(current()))
               && current() != '>')
            pos++;
        return std::string(src + start, pos - start);
    }

    /* ── Node creation helpers ─────────────────────────────── */

    aether_node_s* create_text(const std::string& text, aether_doc_s* doc) {
        auto lease = aether_make_editable_node(doc, AETHER_NODE_TEXT);
        auto* n = lease->node.get();
        n->text = text;
        return n;
    }

    aether_node_s* create_comment(const std::string& text, aether_doc_s* doc) {
        auto lease = aether_make_editable_node(doc, AETHER_NODE_COMMENT);
        auto* n = lease->node.get();
        n->text = text;
        return n;
    }

    aether_node_s* create_element(const std::string& tag, aether_doc_s* doc) {
        auto lease = aether_make_editable_node(doc, AETHER_NODE_ELEMENT);
        auto* n = lease->node.get();
        n->tag_name = tag;
        return n;
    }

    /* ── Parse methods ─────────────────────────────────────── */

    void parse_text(aether_node_s* parent, aether_doc_s* doc) {
        size_t start = pos;
        size_t next = std::string_view(src, len).find('<', pos);
        pos = (next == std::string_view::npos) ? len : next;
        if (pos <= start) return;

        std::string raw(src + start, pos - start);
        std::string decoded = aether::html_decode(raw.c_str());
        if (!decoded.empty()) {
            parent->children.push_back(create_text(decoded, doc));
            parent->children.back()->parent = parent;
        }
    }

    void parse_comment(aether_node_s* parent, aether_doc_s* doc) {
        pos += 4;
        const size_t start = pos;
        enum class State { Start, StartDash, Comment, EndDash, End, EndBang };
        State state = State::Start;
        bool closed = false;
        while (!end() && !closed) {
            const char c = current();
            switch (state) {
            case State::Start:
                if (c == '-') state = State::StartDash;
                else if (c == '>') closed = true;
                else state = State::Comment;
                break;
            case State::StartDash:
                if (c == '-') state = State::End;
                else if (c == '>') closed = true;
                else state = State::Comment;
                break;
            case State::Comment:
                if (c == '-') state = State::EndDash;
                break;
            case State::EndDash:
                state = c == '-' ? State::End : State::Comment;
                break;
            case State::End:
                if (c == '>') closed = true;
                else if (c == '!') state = State::EndBang;
                else if (c != '-') state = State::Comment;
                break;
            case State::EndBang:
                if (c == '-') state = State::EndDash;
                else if (c == '>') closed = true;
                else state = State::Comment;
                break;
            }
            if (!closed) ++pos;
        }

        // Comment data is one contiguous input interval. The state withholds
        // only a possible closing suffix: nothing, '-', nothing, '-', '--',
        // or '--!'. A fallback commits it by advancing into Comment (or the
        // next pending suffix). At both a close and EOF it remains excluded.
        // Keep literal bytes, including CR/CRLF and references; the public
        // C-string boundary makes an embedded NUL EOF before this parser runs.
        constexpr size_t pending[] = {0, 1, 0, 1, 2, 3};
        const size_t text_end = pos - pending[static_cast<size_t>(state)];
        parent->children.push_back(
            create_comment(std::string(src + start, text_end - start), doc));
        parent->children.back()->parent = parent;
        if (closed) ++pos; // Consume exactly the closing '>', never the tail.
    }

    void skip_declaration() {
        size_t end_pos = std::string_view(src, len).find('>', pos + 2);
        pos = (end_pos == std::string_view::npos) ? len : end_pos + 1;
    }

    void skip_processing_instruction() {
        size_t end_pos = std::string_view(src, len).find("?>", pos + 2);
        pos = (end_pos == std::string_view::npos) ? len : end_pos + 2;
    }

    void parse_end_tag(std::vector<aether_node_s*>& stack, aether_doc_s* doc) {
        (void)doc;
        pos += 2;
        const bool immediate_ascii_name = !end() && is_ascii_alpha(current());
        skip_whitespace();
        std::string name = read_name();
        // Restrict the shared tokenizer to recognized names and delimiters.
        // Keep legacy handling for malformed openers, truncated names and EOF
        // immediately after a name, outside this suffix-consumption contract.
        if (immediate_ascii_name && !name.empty() && !end() &&
            is_raw_text_end_boundary(current())) {
            // Consume even an unmatched token, so quoted markup cannot escape
            // its discarded attributes. EOF must not alter insertion context.
            if (consume_end_tag_suffix() == EndTagSuffixResult::Eof) return;
        } else {
            skip_until_tag_end();
        }

        if (name.empty()) return;

        // Find the nearest open match before changing insertion context. A
        // stray close must not discard ancestors; index 0 is the document.
        for (size_t count = stack.size(); count > 1; --count) {
            aether_node_s* node = stack[count - 1];
            if (node->type == AETHER_NODE_ELEMENT &&
                aether::ascii_iequals(node->tag_name, name)) {
                stack.resize(count - 1);
                return;
            }
        }
    }

    void parse_raw_text(aether_node_s* element, aether_doc_s* doc) {
        const std::string_view input(src, len);
        const std::string_view name(element->tag_name);
        size_t search = pos;
        size_t text_end = len;

        while (search < len) {
            const size_t candidate = input.find('<', search);
            if (candidate == std::string_view::npos) break;

            // Require </name plus a real delimiter: EOF is not a boundary.
            // The remaining-length check bounds every subsequent access.
            if (len - candidate > name.size() + 2 && src[candidate + 1] == '/' &&
                aether::ascii_iequals(input.substr(candidate + 2, name.size()), name) &&
                is_raw_text_end_boundary(src[candidate + 2 + name.size()])) {
                text_end = candidate;
                break;
            }
            // Retain rejected candidates verbatim, including overlapping '<'.
            // Names have fixed length (style/script), so the scan is O(n).
            search = candidate + 1;
        }

        if (pos < text_end) {
            element->children.push_back(
                create_text(std::string(src + pos, text_end - pos), doc));
            element->children.back()->parent = element;
        }
        // The existing end-tag parser consumes the accepted close and pops the
        // stack. This shared scan does not implement escaped ScriptData states.
        pos = text_end;
    }

    enum class EndTagSuffixResult { Terminated, Eof };

    EndTagSuffixResult consume_end_tag_suffix() {
        // The name and delimiter have already been recognized. Consume the
        // discarded attributes without decoding references or changing the
        // stack. Only a source '>' outside a quoted value terminates the token.
        enum class State {
            BeforeName, Name, AfterName, BeforeValue, SingleQuoted, DoubleQuoted,
            Unquoted, AfterQuoted, SelfClosing
        };
        State state = State::BeforeName;
        while (!end()) {
            const char c = current();
            const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
            switch (state) {
            case State::BeforeName:
                if (space) { ++pos; break; }
                if (c == '>') { ++pos; return EndTagSuffixResult::Terminated; }
                if (c == '/') { ++pos; state = State::SelfClosing; break; }
                // Even '=' or a quote is a first attribute-name byte here.
                ++pos;
                state = State::Name;
                break;
            case State::Name:
                if (space || c == '/' || c == '>') { state = State::AfterName; break; }
                ++pos;
                if (c == '=') state = State::BeforeValue;
                break;
            case State::AfterName:
                if (space) { ++pos; break; }
                if (c == '>') { ++pos; return EndTagSuffixResult::Terminated; }
                if (c == '/') { ++pos; state = State::SelfClosing; break; }
                if (c == '=') { ++pos; state = State::BeforeValue; break; }
                state = State::BeforeName;
                break;
            case State::BeforeValue:
                if (space) { ++pos; break; }
                if (c == '>') { ++pos; return EndTagSuffixResult::Terminated; }
                if (c == '\'' || c == '"') {
                    ++pos;
                    state = c == '\'' ? State::SingleQuoted : State::DoubleQuoted;
                    break;
                }
                state = State::Unquoted;
                break;
            case State::SingleQuoted:
            case State::DoubleQuoted:
                ++pos;
                if (c == (state == State::SingleQuoted ? '\'' : '"')) state = State::AfterQuoted;
                break;
            case State::Unquoted:
                if (c == '>') { ++pos; return EndTagSuffixResult::Terminated; }
                ++pos;
                if (space) state = State::BeforeName;
                break;
            case State::AfterQuoted:
                if (space) { ++pos; state = State::BeforeName; break; }
                if (c == '>') { ++pos; return EndTagSuffixResult::Terminated; }
                if (c == '/') { ++pos; state = State::SelfClosing; break; }
                state = State::BeforeName;
                break;
            case State::SelfClosing:
                if (c == '>') { ++pos; return EndTagSuffixResult::Terminated; }
                state = State::BeforeName;
                break;
            }
        }
        // EOF in an accepted end-tag token discards it, including its name.
        return EndTagSuffixResult::Eof;
    }

    void parse_rcdata(aether_node_s* element, aether_doc_s* doc) {
        const std::string_view input(src, len);
        const std::string_view name(element->tag_name);
        const size_t text_start = pos;
        size_t text_end = len;
        size_t search = pos;
        while (search < len) {
            const size_t candidate = input.find('<', search);
            if (candidate == std::string_view::npos) break;
            // Recognize syntax in the source, never in decoded text. EOF after
            // the name is not a delimiter; rejected candidates remain literal.
            if (len - candidate > name.size() + 2 && src[candidate + 1] == '/' &&
                aether::ascii_iequals(input.substr(candidate + 2, name.size()), name) &&
                is_raw_text_end_boundary(src[candidate + 2 + name.size()])) {
                text_end = candidate;
                pos = candidate + 2 + name.size();
                (void)consume_end_tag_suffix();
                break;
            }
            search = candidate + 1;
        }
        if (text_end == len) pos = len;

        std::string normalized;
        normalized.reserve(text_end - text_start);
        for (size_t i = text_start; i < text_end; ++i) {
            if (src[i] == '\r') {
                normalized.push_back('\n');
                if (i + 1 < text_end && src[i + 1] == '\n') ++i;
            } else {
                normalized.push_back(src[i]);
            }
        }
        // Keep the existing HTML4 named-reference decoder. A decoded CR is not
        // source preprocessing, and decoded markup must never be tokenized.
        std::string decoded = aether::html_decode(normalized.c_str());
        if (name == "textarea" && !decoded.empty() && decoded.front() == '\n')
            decoded.erase(0, 1);
        if (!decoded.empty()) {
            element->children.push_back(create_text(decoded, doc));
            element->children.back()->parent = element;
        }
    }

    void parse_start_tag(std::vector<aether_node_s*>& stack, aether_doc_s* doc) {
        pos++;
        // Only an immediate ASCII letter opens an ordinary start tag. Emit
        // just '<' otherwise, leaving the next byte for the normal dispatcher.
        if (end() || !is_ascii_alpha(current())) {
            stack.back()->children.push_back(create_text("<", doc));
            stack.back()->children.back()->parent = stack.back();
            return;
        }
        std::string tag_name = read_name();
        // read_name still has its narrower legacy alphabet. Do not let a
        // truncated name such as "textarea+foo" accidentally enable RCDATA.
        const bool rcdata_name_boundary = !end() && is_raw_text_end_boundary(current());
        // Canonical HTML element identity. Attribute names are normalized at
        // their own storage boundary below; values remain untouched.
        tag_name = aether::ascii_lowercase(std::move(tag_name));

        // Stage the token separately from the DOM: EOF in any attribute state
        // discards the entire opening without allocating a node or style ID.
        std::map<std::string, std::string, std::less<>> attributes;
        bool tag_terminated = false;
        bool self_closing = false;

        while (!end()) {
            skip_whitespace();
            if (end()) break;

            if (current() == '>') {
                pos++;
                tag_terminated = true;
                break;
            }

            if (current() == '/' && peek(1) == '>') {
                self_closing = true;
                pos += 2;
                tag_terminated = true;
                break;
            }

            std::string attr_name = aether::canonical_html_attribute_name(read_name());
            if (attr_name.empty()) {
                pos++;
                continue;
            }

            skip_whitespace();
            std::string attr_value;
            if (!end() && current() == '=') {
                pos++;
                skip_whitespace();
                attr_value = read_attribute_value();
            }

            std::string decoded_val = aether::html_decode(attr_value.c_str());
            // First source occurrence per ASCII name, after consuming each value.
            attributes.try_emplace(attr_name, decoded_val);
        }

        // Only a syntactic terminator confirms the token. In particular, '>'
        // consumed inside a quoted value is data, even as the final input byte.
        if (!tag_terminated) return;

        auto pending = create_element(tag_name, doc);
        pending->attributes = std::move(attributes);
        if (auto value=pending->attributes.find("value"); value!=pending->attributes.end()) {
            pending->authored_value_present=true; pending->authored_value=value->second;
        }
        stack.back()->children.push_back(pending);
        pending->parent = stack.back();
        // The tree takes ownership only after push_back succeeds.
        aether_node_s* element = pending;

        // HTML ignores the self-closing flag for these non-void elements.
        // This path owns its close/EOF and never exposes RCDATA to dispatch.
        if (rcdata_name_boundary && (tag_name == "textarea" || tag_name == "title")) {
            parse_rcdata(element, doc);
            return;
        }

        if (self_closing || is_void_element(element->tag_name))
            return;

        stack.push_back(element);
        if (aether::ascii_iequals(element->tag_name, "style") ||
            aether::ascii_iequals(element->tag_name, "script")) {
            parse_raw_text(element, doc);
        }
    }

public:
    HtmlParser(const char* source, size_t length)
        : src(source), len(length), pos(0) {}

    aether_node_s* parse_document(aether_doc_s* doc) {
        std::vector<aether_node_s*> stack;
        stack.push_back(doc);

        while (!end()) {
            if (current() != '<') {
                parse_text(stack.back(), doc);
                continue;
            }

            if (starts_with("<!--")) {
                parse_comment(stack.back(), doc);
                continue;
            }

            if (starts_with("<!")) {
                skip_declaration();
                continue;
            }

            if (starts_with("</")) {
                parse_end_tag(stack, doc);
                continue;
            }

            if (starts_with("<?")) {
                skip_processing_instruction();
                continue;
            }

            parse_start_tag(stack, doc);
        }

        return doc;
    }
};

/* ── C ABI entry point ────────────────────────────────────────────── */

AETHER_API aether_status_t aether_doc_parse_html(aether_doc_t doc, const char* html) {
    if (!doc || !html) return AETHER_ERROR_NULL_ARG;

    const bool bound=!doc->owner_control->binding.expired();
    if(bound&&!aether_dom_valid_string(html))return AETHER_ERROR_INVALID_STATE;
    if (!aether_dom_can_touch(doc, 1, true) || doc->version == INT64_MAX) return AETHER_ERROR_INVALID_STATE;
    try {
        // Parse into private ownership. No published input, ID or epoch changes on failure.
        aether_doc_s candidate;
        HtmlParser parser(html, std::strlen(html)); parser.parse_document(&candidate);
        if(bound)for(const auto& [node,storage]:candidate.editable_nodes) {
            if(!aether_dom_valid_string(node->tag_name)||!aether_dom_valid_string(node->text))return AETHER_ERROR_INVALID_STATE;
            for(const auto& [name,value]:node->attributes)if(!aether_dom_valid_string(name)||!aether_dom_valid_string(value))return AETHER_ERROR_INVALID_STATE;
        }
        DomStyleSlotStageRef style_stage;
        if(auto binding=doc->owner_control->binding.lock()) {
            std::string source;auto status=aether_dom_collect_style_source(&candidate,source);if(status!=AETHER_OK)return status;
            status=aether_dom_style_prepare_slot(binding,source,style_stage);if(status!=AETHER_OK)return status;
        }
        std::vector<aether_node_s*> retiring;
        retiring.reserve(doc->editable_nodes.size());
        for (const auto& [node, storage] : doc->editable_nodes) {
            if(node->identity->disconnect_serial==UINT64_MAX)return AETHER_ERROR_INVALID_STATE;
            retiring.push_back(node);
        }
        DomInteractionStageRef interactions;
        auto interaction_status=aether_dom_interaction_prepare_stage(doc->owner_control,retiring,nullptr,nullptr,interactions,true);
        if(interaction_status!=AETHER_OK)return interaction_status;
        if(!aether_dom_interaction_validate_stage(interactions))return AETHER_ERROR_INVALID_STATE;
        if(style_stage&&!aether_dom_style_validate_slot(style_stage))return AETHER_ERROR_INVALID_STATE;
        // All allocations above; below this point only swaps and scalar stores.
        aether_dom_interaction_commit_stage(interactions);
        for (auto* node : retiring) aether_dom_retire_node(node);
        for (auto& [node, storage] : candidate.editable_nodes) {
            node->document_owner=doc->owner_control; node->identity->document_owner=doc->owner_control;
        }
        for (auto* node : candidate.children) node->parent=doc;
        doc->children.swap(candidate.children); doc->editable_nodes.swap(candidate.editable_nodes);
        doc->id_index.swap(candidate.id_index); doc->next_style_node_id=candidate.next_style_node_id;
        aether_dom_dispose_unmanaged_children(&candidate);
        for (auto& [node, storage] : candidate.editable_nodes) aether_dom_dispose_unmanaged_children(node);
        ++doc->version; aether_node_touch_typed(doc, aether::dom::MutationKind::structure, true);
        if(style_stage)aether_dom_style_commit_slot(style_stage);
        return AETHER_OK;
    } catch(const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch(...) { return AETHER_ERROR_INVALID_STATE; }
}
