#include "dom_types.h"
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
    std::string lower = tag;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
    for (auto* v : voids) {
        if (lower == v) return true;
    }
    return false;
}

static char ascii_lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}

static bool ascii_equal_ignore_case(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    }
    return true;
}

static bool is_raw_text_end_boundary(char c) {
    // HTML whitespace, not locale-sensitive isspace (which also accepts VT).
    // Unlike a browser tokenizer, this byte parser has not normalized CR to LF.
    return c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\n' ||
           c == '\f' || c == '\r';
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
        auto* n = new aether_node_s();
        n->type = AETHER_NODE_TEXT;
        n->text = text;
        n->style_node_id = doc->next_style_node_id++;
        return n;
    }

    aether_node_s* create_comment(const std::string& text, aether_doc_s* doc) {
        auto* n = new aether_node_s();
        n->type = AETHER_NODE_COMMENT;
        n->text = text;
        n->style_node_id = doc->next_style_node_id++;
        return n;
    }

    std::unique_ptr<aether_node_s> create_element(const std::string& tag, aether_doc_s* doc) {
        auto n = std::make_unique<aether_node_s>();
        n->type = AETHER_NODE_ELEMENT;
        n->tag_name = tag;
        n->style_node_id = doc->next_style_node_id++;
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
        std::string_view sv(src, len);
        size_t end_pos = sv.find("-->", pos);
        if (end_pos == std::string_view::npos) {
            parent->children.push_back(
                create_comment(std::string(src + pos, len - pos), doc));
            parent->children.back()->parent = parent;
            pos = len;
            return;
        }
        parent->children.push_back(
            create_comment(std::string(src + pos, end_pos - pos), doc));
        parent->children.back()->parent = parent;
        pos = end_pos + 3;
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
        skip_whitespace();
        std::string name = read_name();
        skip_until_tag_end();

        if (name.empty()) return;

        // Find the nearest open match before changing insertion context. A
        // stray close must not discard ancestors; index 0 is the document.
        for (size_t count = stack.size(); count > 1; --count) {
            aether_node_s* node = stack[count - 1];
            if (node->type == AETHER_NODE_ELEMENT &&
                ascii_equal_ignore_case(node->tag_name, name)) {
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
                ascii_equal_ignore_case(input.substr(candidate + 2, name.size()), name) &&
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
        // Canonical HTML element identity. Attribute names are normalized at
        // their own storage boundary below; values remain untouched.
        std::transform(tag_name.begin(), tag_name.end(), tag_name.begin(), ascii_lower);

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
        stack.back()->children.push_back(pending.get());
        pending->parent = stack.back();
        // The tree takes ownership only after push_back succeeds.
        aether_node_s* element = pending.release();

        if (self_closing || is_void_element(element->tag_name))
            return;

        stack.push_back(element);
        if (ascii_equal_ignore_case(element->tag_name, "style") ||
            ascii_equal_ignore_case(element->tag_name, "script")) {
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

    // Parsing is replacement, not append.  Reusing a document handle is the normal
    // path for the native style bridge during component rebuilds, so retaining the
    // previous children here would leak an entire DOM snapshot and make selectors
    // operate over stale nodes.
    std::function<void(aether_node_s*)> destroy_subtree = [&](aether_node_s* node) {
        for (auto* child : node->children) destroy_subtree(child);
        delete node;
    };
    for (auto* child : doc->children) destroy_subtree(child);
    doc->children.clear();
    doc->id_index.clear();
    doc->next_style_node_id = 1;

    size_t length = std::strlen(html);
    HtmlParser parser(html, length);
    parser.parse_document(doc);
    doc->version++;
    return AETHER_OK;
}
