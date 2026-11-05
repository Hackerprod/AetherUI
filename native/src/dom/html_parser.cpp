#include "dom_types.h"
#include "../abi/aether_abi.h"
#include <cctype>
#include <cstring>
#include <algorithm>
#include <stack>

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

static std::string to_lower_copy(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
        [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
    return r;
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

    aether_node_s* create_element(const std::string& tag, aether_doc_s* doc) {
        auto* n = new aether_node_s();
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

    void parse_end_tag(std::stack<aether_node_s*>& stack, aether_doc_s* doc) {
        (void)doc;
        pos += 2;
        skip_whitespace();
        std::string name = to_lower_copy(read_name());
        skip_until_tag_end();

        if (name.empty()) return;

        while (stack.size() > 1) {
            aether_node_s* node = stack.top();
            stack.pop();
            if (node->type == AETHER_NODE_ELEMENT &&
                to_lower_copy(node->tag_name) == name) {
                break;
            }
        }
    }

    void parse_raw_text(aether_node_s* element, aether_doc_s* doc) {
        std::string close_tag = "</" + to_lower_copy(element->tag_name);
        size_t search = pos;

        while (search < len) {
            std::string_view sv(src, len);
            size_t candidate = sv.find(close_tag.c_str(), search);
            if (candidate == std::string_view::npos) {
                if (pos < len) {
                    element->children.push_back(
                        create_text(std::string(src + pos, len - pos), doc));
                    element->children.back()->parent = element;
                }
                pos = len;
                return;
            }

            size_t after = candidate + close_tag.length();
            if (after >= len || src[after] == '>' ||
                std::isspace(static_cast<unsigned char>(src[after]))) {
                if (candidate > pos) {
                    element->children.push_back(
                        create_text(std::string(src + pos, candidate - pos), doc));
                    element->children.back()->parent = element;
                }
                pos = candidate;
                return;
            }
            search = after;
        }

        if (pos < len) {
            element->children.push_back(
                create_text(std::string(src + pos, len - pos), doc));
            element->children.back()->parent = element;
        }
        pos = len;
    }

    void parse_start_tag(std::stack<aether_node_s*>& stack, aether_doc_s* doc) {
        size_t tag_start = pos;
        pos++;
        skip_whitespace();
        std::string tag_name = read_name();
        if (tag_name.empty()) {
            stack.top()->children.push_back(create_text("<", doc));
            stack.top()->children.back()->parent = stack.top();
            pos = std::min(tag_start + 1, len);
            return;
        }

        aether_node_s* element = create_element(tag_name, doc);
        bool self_closing = false;

        while (!end()) {
            skip_whitespace();
            if (end()) break;

            if (current() == '>') {
                pos++;
                break;
            }

            if (current() == '/' && peek(1) == '>') {
                self_closing = true;
                pos += 2;
                break;
            }

            std::string attr_name = read_name();
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
            element->attributes[attr_name] = decoded_val;
        }

        stack.top()->children.push_back(element);
        element->parent = stack.top();

        if (self_closing || is_void_element(element->tag_name))
            return;

        stack.push(element);
        if (element->tag_name == "style" || element->tag_name == "script") {
            parse_raw_text(element, doc);
        }
    }

public:
    HtmlParser(const char* source, size_t length)
        : src(source), len(length), pos(0) {}

    aether_node_s* parse_document(aether_doc_s* doc) {
        std::stack<aether_node_s*> stack;
        stack.push(doc);

        while (!end()) {
            if (current() != '<') {
                parse_text(stack.top(), doc);
                continue;
            }

            if (starts_with("<!--")) {
                parse_comment(stack.top(), doc);
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
