#include "dom_types.h"
#include "html_attribute_name.h"
#include "owned_parser.h"
#include "../rendering/html_ltr/owned_allocator.h"
#include <type_traits>
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
    return c == '>' || c == '/' || aether::is_html_space(static_cast<unsigned char>(c));
}

/* ── Parser ───────────────────────────────────────────────────────── */

template<class TemporaryAllocator>
class HtmlParser {
    using String=std::basic_string<char,std::char_traits<char>,TemporaryAllocator>;
    using Stack=std::vector<aether_node_s*,typename std::allocator_traits<TemporaryAllocator>::template rebind_alloc<aether_node_s*>>;
    TemporaryAllocator allocator;
    const char* src;
    size_t len;
    size_t pos;
    bool skip_leading_lf = false;

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

    String read_name() {
        size_t start = pos;
        while (!end() && is_name_char(current()))
            pos++;
        return String(src + start, pos - start, allocator);
    }

    void skip_until_tag_end() {
        while (!end() && current() != '>')
            pos++;
        if (!end()) pos++;
    }

    String read_attribute_value() {
        // Only HTML whitespace precedes a value; VT starts unquoted data.
        while (!end() && aether::is_html_space(static_cast<unsigned char>(current())))
            pos++;
        if (end()) return String(allocator);

        if (current() == '\'' || current() == '"') {
            char quote = current();
            pos++;
            size_t start = pos;
            while (!end() && current() != quote)
                pos++;
            String val(src + start, pos - start, allocator);
            if (!end()) pos++;
            return val;
        }

        // In an unquoted value, '/' is data, including immediately before '>'.
        // The outer tag parser recognizes '/>' only outside an attribute value.
        size_t start = pos;
        while (!end() && !aether::is_html_space(static_cast<unsigned char>(current()))
               && current() != '>')
            pos++;
        return String(src + start, pos - start, allocator);
    }

    /* ── Node creation helpers ─────────────────────────────── */

    std::unique_ptr<aether_node_s> create_text(std::string_view text, aether_doc_s* doc) {
        auto n = std::make_unique<aether_node_s>();
        n->type = AETHER_NODE_TEXT;
        n->text.assign(text.data(),text.size());
        n->style_node_id = doc->next_style_node_id++;
        return n;
    }

    std::unique_ptr<aether_node_s> create_comment(std::string_view text, aether_doc_s* doc) {
        auto n = std::make_unique<aether_node_s>();
        n->type = AETHER_NODE_COMMENT;
        n->text.assign(text.data(),text.size());
        n->style_node_id = doc->next_style_node_id++;
        return n;
    }

    // Keep each pending node owned across text assignment and vector growth.
    // Legacy parsing is still replacement, not a transactional DOM operation.
    static void append_owned(aether_node_s* parent, std::unique_ptr<aether_node_s> node) {
        parent->children.push_back(node.get());
        node->parent = parent;
        (void)node.release();
    }

    std::unique_ptr<aether_node_s> create_element(std::string_view tag, aether_doc_s* doc) {
        auto n = std::make_unique<aether_node_s>();
        n->type = AETHER_NODE_ELEMENT;
        n->tag_name.assign(tag.data(),tag.size());
        n->style_node_id = doc->next_style_node_id++;
        return n;
    }

    /* ── Parse methods ─────────────────────────────────────── */

    void parse_text(aether_node_s* parent, aether_doc_s* doc) {
        size_t start = pos;
        size_t next = std::string_view(src, len).find('<', pos);
        pos = (next == std::string_view::npos) ? len : next;
        if (pos <= start) return;

        String raw(src + start, pos - start, allocator);
        String decoded = aether::html_decode_storage(raw.c_str(),String(allocator));
        // pre/listing suppress exactly one eligible character token, after
        // source preprocessing and reference decoding, never at DOM mutation.
        if (skip_leading_lf) {
            skip_leading_lf = false;
            if (!decoded.empty() && decoded.front() == '\n') decoded.erase(0, 1);
        }
        if (!decoded.empty()) {
            append_owned(parent, create_text(decoded, doc));
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
        // Keep preprocessed source bytes and literal references; the public
        // C-string boundary makes an embedded NUL EOF before this parser runs.
        constexpr size_t pending[] = {0, 1, 0, 1, 2, 3};
        const size_t text_end = pos - pending[static_cast<size_t>(state)];
        append_owned(parent,
            create_comment(String(src + start, text_end - start, allocator), doc));
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

    void parse_end_tag(Stack& stack, aether_doc_s* doc) {
        (void)doc;
        pos += 2;
        const bool immediate_ascii_name = !end() && is_ascii_alpha(current());
        skip_whitespace();
        String name = read_name();
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
            append_owned(element,
                create_text(String(src + pos, text_end - pos, allocator), doc));
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

        String raw(src + text_start, text_end - text_start, allocator);
        // Keep the existing HTML4 named-reference decoder. A decoded CR is not
        // source preprocessing, and decoded markup must never be tokenized.
        String decoded = aether::html_decode_storage(raw.c_str(),String(allocator));
        if (name == "textarea" && !decoded.empty() && decoded.front() == '\n')
            decoded.erase(0, 1);
        if (!decoded.empty()) {
            append_owned(element, create_text(decoded, doc));
        }
    }

    void parse_start_tag(Stack& stack, aether_doc_s* doc) {
        pos++;
        // Only an immediate ASCII letter opens an ordinary start tag. Emit
        // just '<' otherwise, leaving the next byte for the normal dispatcher.
        if (end() || !is_ascii_alpha(current())) {
            append_owned(stack.back(), create_text("<", doc));
            return;
        }
        String tag_name = read_name();
        // read_name still has its narrower legacy alphabet. Do not let a
        // truncated name such as "textarea+foo" or "pre+foo" enable special
        // text handling just because its stored DOM name matches.
        const bool special_text_name_boundary = !end() && is_raw_text_end_boundary(current());
        // Canonical HTML element identity. Attribute names are normalized at
        // their own storage boundary below; values remain untouched.
        for(char& byte:tag_name)byte=static_cast<char>(aether::ascii_lower_byte(static_cast<unsigned char>(byte)));

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

            String attr_name = read_name();
            for(char& byte:attr_name)byte=static_cast<char>(aether::ascii_lower_byte(static_cast<unsigned char>(byte)));
            if (attr_name.empty()) {
                pos++;
                continue;
            }

            skip_whitespace();
            String attr_value(allocator);
            if (!end() && current() == '=') {
                pos++;
                attr_value = read_attribute_value();
            }

            String decoded_val = aether::html_decode_storage(attr_value.c_str(),String(allocator));
            // First source occurrence per ASCII name, after consuming each value.
            if constexpr(std::is_same_v<TemporaryAllocator,std::allocator<char>>) {
                attributes.try_emplace(attr_name, decoded_val);
            } else {
                // This map is moved into the unchanged legacy DOM. It is
                // retained DOM storage, explicitly NOT counted as temporary.
                const std::string_view key(attr_name.data(),attr_name.size());
                if(attributes.find(key)==attributes.end())attributes.emplace(
                    std::piecewise_construct,
                    std::forward_as_tuple(key.data(),key.size()),
                    std::forward_as_tuple(decoded_val.data(),decoded_val.size()));
            }
        }

        // Only a syntactic terminator confirms the token. In particular, '>'
        // consumed inside a quoted value is data, even as the final input byte.
        if (!tag_terminated) return;

        auto pending = create_element(tag_name, doc);
        pending->attributes = std::move(attributes);
        stack.back()->children.push_back(pending.get());
        pending->parent = stack.back();
        // The tree takes ownership only after push_back succeeds.
        aether_node_s* element = pending.release();

        // HTML ignores the self-closing flag for these non-void elements.
        // This path owns its close/EOF and never exposes RCDATA to dispatch.
        if (special_text_name_boundary && (tag_name == "textarea" || tag_name == "title")) {
            parse_rcdata(element, doc);
            return;
        }

        if (self_closing || is_void_element(element->tag_name))
            return;

        stack.push_back(element);
        if (special_text_name_boundary && (tag_name == "pre" || tag_name == "listing"))
            skip_leading_lf = true;
        if (aether::ascii_iequals(element->tag_name, "style") ||
            aether::ascii_iequals(element->tag_name, "script")) {
            parse_raw_text(element, doc);
        }
    }

public:
    HtmlParser(const char* source, size_t length,TemporaryAllocator a)
        : allocator(a),src(source), len(length), pos(0) {}

    aether_node_s* parse_document(aether_doc_s* doc) {
        Stack stack{typename Stack::allocator_type(allocator)};
        stack.push_back(doc);

        while (!end()) {
            if (current() != '<') {
                parse_text(stack.back(), doc);
                continue;
            }

            // The exact empty end-tag spelling emits no token. It neither
            // consumes nor rearms the pending opportunity, even when repeated.
            if (starts_with("</>")) {
                pos += 3;
                continue;
            }
            // Every other markup path consumes it, including ignored tokens
            // and an invalid '<' emitted literally by parse_start_tag.
            skip_leading_lf = false;

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

template<class TemporaryAllocator>
static aether_status_t parse_html_storage(aether_doc_t doc, const char* html,TemporaryAllocator allocator) {
    if (!doc || !html) return AETHER_ERROR_NULL_ARG;

    // WHATWG input preprocessing applies to the whole source, before tokenization
    // and reference decoding. Keep the original pointer on the no-CR fast path.
    // The owned buffer, when needed, outlives every parser view; DOM strings copy.
    size_t length = std::strlen(html);
    std::basic_string<char,std::char_traits<char>,TemporaryAllocator> normalized(allocator);
    const char* first_cr = static_cast<const char*>(std::memchr(html, '\r', length));
    if (first_cr) {
        // Allocate before replacing the previous DOM. This particular failure
        // leaves it intact; later parser allocations still have no rollback guarantee.
        normalized.assign(html, length);
        size_t write = static_cast<size_t>(first_cr - html);
        for (size_t read = write; read < length; ++read) {
            if (normalized[read] == '\r') {
                normalized[write++] = '\n';
                if (read + 1 < length && normalized[read + 1] == '\n') ++read;
            } else {
                normalized[write++] = normalized[read];
            }
        }
        normalized.resize(write);
        html = normalized.c_str();
        length = normalized.size();
    }

    // Parsing is replacement, not append.  Reusing a document handle is the normal
    // path for the native style bridge during component rebuilds, so retaining the
    // previous children here would leak an entire DOM snapshot and make selectors
    // operate over stale nodes.
    aether_node_touch(doc, true);
    for (auto* child : doc->children) aether_destroy_dom_subtree(child);
    doc->children.clear();
    doc->id_index.clear();
    doc->next_style_node_id = 1;

    HtmlParser<TemporaryAllocator> parser(html, length,allocator);
    parser.parse_document(doc);
    doc->version++;
    return AETHER_OK;
}

AETHER_API aether_status_t aether_doc_parse_html(aether_doc_t doc, const char* html) {
    return parse_html_storage(doc,html,std::allocator<char>{});
}

namespace aether::dom {
aether_status_t parse_html_owned_temporaries(aether_doc_t doc,const char* html,
    rendering::html_ltr::OwnedBudget::Handle domain) {
    using namespace rendering::html_ltr;
    try {return parse_html_storage(doc,html,OwnedAllocator<char>(std::move(domain),OwnedCategory::temporary));}
    catch(const Error& e){return e.status;}
    catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}
}
}
