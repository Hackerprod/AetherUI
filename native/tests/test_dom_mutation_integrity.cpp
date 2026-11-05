// Headless public-ABI regression. All checks remain active under NDEBUG.
#include <aether/aether.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

static void require(bool ok, const std::string& message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
        // A broken implementation can create a cycle. Do not invoke recursive
        // indexing, style/layout work, or owning destructors after a failure.
        std::exit(1);
    }
}
static std::string owned_string(char* value) {
    std::string result = value ? value : "";
    aether_string_free(value);
    return result;
}
static std::vector<aether_node_t> children(aether_node_t node) {
    const int count = aether_node_get_child_count(node);
    require(count >= 0 && count <= 128, "bounded child count");
    std::vector<aether_node_t> result;
    for (int i = 0; i < count; ++i) {
        auto child = aether_node_get_child(node, i);
        require(child != nullptr, "non-null enumerated child");
        require(std::find(result.begin(), result.end(), child) == result.end(), "no duplicate children");
        result.push_back(child);
    }
    require(aether_node_get_child(node, count) == nullptr, "child count agrees with enumeration");
    return result;
}
static void add_unique(std::vector<aether_node_t>& nodes, aether_node_t node) {
    if (node && std::find(nodes.begin(), nodes.end(), node) == nodes.end()) nodes.push_back(node);
}
static std::vector<aether_node_t> collect_authored(aether_node_t root) {
    std::vector<aether_node_t> result{root};
    for (size_t i = 0; i < result.size(); ++i) {
        require(result.size() <= 128, "bounded authored traversal");
        for (auto child : children(result[i])) {
            require(std::find(result.begin(), result.end(), child) == result.end(), "acyclic authored traversal");
            result.push_back(child);
        }
    }
    return result;
}
static void check_forest(const std::vector<aether_node_t>& authored) {
    // Independent bounded inspection, including disconnected components, before
    // using any recursive production consumer. Synthetic aliases are excluded.
    for (auto node : authored) {
        std::set<aether_node_t> seen;
        for (auto p = node; p; p = aether_node_get_parent(p)) {
            require(seen.size() < 128 && seen.insert(p).second, "finite parent chain");
        }
        for (auto child : children(node)) {
            require(aether_node_get_parent(child) == node, "reciprocal authored edge");
            require(std::find(authored.begin(), authored.end(), child) != authored.end(), "known authored child");
        }
        if (auto parent = aether_node_get_parent(node)) {
            const auto siblings = children(parent);
            require(std::count(siblings.begin(), siblings.end(), node) == 1, "reciprocal authored parent");
        }
    }
}
struct NodeState {
    aether_node_t node, parent;
    std::vector<aether_node_t> children;
    aether_node_type_t type;
    int64_t style_id;
    std::string text;
    bool operator==(const NodeState&) const = default;
};
struct Snapshot {
    int64_t version;
    std::vector<NodeState> nodes;
    std::vector<aether_node_t> indexed;
    bool operator==(const Snapshot&) const = default;
};
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_node_t body = nullptr;
    std::vector<aether_node_t> authored, known;
    std::vector<std::string> ids;
    Fixture(const char* html, std::vector<std::string> ids_) : ids(std::move(ids_)) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc, html) == AETHER_OK, "parse fixture");
        body = aether_doc_get_body(doc);
        require(body != nullptr, "fixture body");
        auto root = body;
        for (int i = 0; aether_node_get_parent(root); ++i) {
            require(i < 128, "bounded initial root lookup");
            root = aether_node_get_parent(root);
        }
        authored = collect_authored(root);
        known = authored;
        check_forest(authored);
    }
    ~Fixture() { check_forest(authored); aether_doc_destroy(doc); }
    aether_node_t id(const char* value) { return aether_doc_get_element_by_id(doc, value); }
    Snapshot snapshot() const {
        Snapshot result{aether_doc_get_version(doc), {}, {}};
        for (auto node : known) result.nodes.push_back({node, aether_node_get_parent(node), children(node),
            aether_node_get_type(node), aether_node_get_style_node_id(node),
            owned_string(aether_node_get_text_content(node))});
        check_forest(authored);
        for (const auto& id : ids) result.indexed.push_back(aether_doc_get_element_by_id(doc, id.c_str()));
        return result;
    }
    void reject(aether_node_t parent, aether_node_t child, bool remove, const char* label,
                aether_status_t expected = AETHER_ERROR_INVALID_STATE) {
        auto before = snapshot();
        auto status = remove ? aether_node_remove_child(parent, child) : aether_node_append_child(parent, child);
        require(status == expected, std::string(label) + ": expected rejection before any recursive consumer");
        require(snapshot() == before, std::string(label) + ": unchanged parents, order, content, IDs and version");
    }
    void append(aether_node_t parent, aether_node_t child) {
        require(aether_node_append_child(parent, child) == AETHER_OK, "valid append");
        check_forest(authored);
    }
    void remove(aether_node_t parent, aether_node_t child) {
        require(aether_node_remove_child(parent, child) == AETHER_OK, "valid remove");
        check_forest(authored);
    }
};
static aether_text_metrics_t measure(const char* text, const aether_computed_style_t* style, float, void*) {
    return {static_cast<float>(text ? std::strlen(text) : 0) * style->font_size / 2,
            style->font_size, style->font_size * .8f};
}
struct Runtime {
    aether_style_runtime_t runtime = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_display_list_builder_t builder = nullptr;
    explicit Runtime(const char* css) {
        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{640, 480, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
                "create runtime");
        int64_t sheet = 0; int32_t rules = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css, 0, "dom-integrity", &sheet, &rules) == AETHER_OK,
                "add stylesheet");
        require(aether_layout_create(measure, nullptr, &engine) == AETHER_OK, "create layout engine");
        require(aether_display_list_builder_create(measure, nullptr, &builder) == AETHER_OK, "create painter");
    }
    ~Runtime() {
        aether_display_list_builder_destroy(builder);
        aether_layout_destroy(engine);
        aether_style_runtime_destroy(runtime);
    }
    void resolve(Fixture& f) {
        check_forest(f.authored);
        aether_style_update_t* updates = nullptr;
        const auto count = aether_style_runtime_resolve_subtree(runtime, f.body, &updates);
        require(count >= 0, "resolve after checked mutations");
        aether_style_updates_free(updates, count);
    }
};
struct Layout {
    aether_style_map_t styles = nullptr;
    aether_layout_result_t result = nullptr;
    Layout(Fixture& f, Runtime& runtime) {
        check_forest(f.authored);
        require(aether_style_runtime_create_resolved_style_map(runtime.runtime, f.doc, &styles) == AETHER_OK,
                "create resolved map");
        require(aether_layout_layout(runtime.engine, f.doc, styles, 640, 480, nullptr, nullptr, &result) == AETHER_OK,
                "create layout");
    }
    ~Layout() { aether_layout_result_destroy(result); aether_style_map_destroy(styles); }
    std::vector<aether_layout_box_t> boxes() const {
        std::vector<aether_layout_box_t> result_boxes{aether_layout_result_get_root(result)};
        require(result_boxes[0] != nullptr, "layout root");
        for (size_t i = 0; i < result_boxes.size(); ++i) {
            require(result_boxes.size() <= 256, "bounded layout traversal");
            const int count = aether_layout_box_get_child_count(result_boxes[i]);
            require(count >= 0 && count <= 128, "bounded layout child count");
            for (int j = 0; j < count; ++j) {
                auto child = aether_layout_box_get_child(result_boxes[i], j);
                require(child && std::find(result_boxes.begin(), result_boxes.end(), child) == result_boxes.end(),
                        "acyclic layout boxes");
                result_boxes.push_back(child);
            }
        }
        return result_boxes;
    }
};
static void rect(std::ostream& out, aether_rectf value) {
    out << value.x << ',' << value.y << ',' << value.width << ',' << value.height << ';';
}
static void color(std::ostream& out, aether_color_rgba value) {
    out << value.r << ',' << value.g << ',' << value.b << ',' << value.a << ';';
}
static void radii(std::ostream& out, aether_corner_radii v) {
    out << v.top_left_x << ',' << v.top_left_y << ',' << v.top_right_x << ',' << v.top_right_y << ','
        << v.bottom_right_x << ',' << v.bottom_right_y << ',' << v.bottom_left_x << ',' << v.bottom_left_y << ';';
}
static std::string geometry(const Layout& layout) {
    std::ostringstream out; out << std::setprecision(9);
    const auto boxes = layout.boxes();
    for (auto box : boxes) {
        out << aether_layout_box_get_kind(box) << ':' << aether_layout_box_get_child_count(box) << ':'
            << aether_layout_box_get_x(box) << ',' << aether_layout_box_get_y(box) << ','
            << aether_layout_box_get_width(box) << ',' << aether_layout_box_get_height(box) << ';';
        rect(out, aether_layout_box_get_margin_box(box)); rect(out, aether_layout_box_get_border_box(box));
        rect(out, aether_layout_box_get_padding_box(box)); rect(out, aether_layout_box_get_content_box(box));
        out << std::quoted(owned_string(aether_layout_box_get_text(box))) << '\n';
    }
    const int count = aether_layout_result_get_paint_order_count(layout.result);
    require(count >= 0 && count <= 256, "bounded paint order");
    out << "paint:";
    for (int i = 0; i < count; ++i) {
        auto box = aether_layout_result_get_paint_order_box(layout.result, i);
        const auto found = std::find(boxes.begin(), boxes.end(), box);
        require(found != boxes.end(), "paint order references published box");
        out << std::distance(boxes.begin(), found) << ',';
    }
    return out.str();
}
static std::string display_list(const Layout& layout, Runtime& runtime) {
    aether_display_list_t list = nullptr;
    require(aether_display_list_build(runtime.builder, layout.result, nullptr, &list) == AETHER_OK, "build display list");
    std::ostringstream out; out << std::setprecision(9);
    const int count = aether_display_list_get_count(list);
    require(count > 0 && count < 1024, "non-empty bounded display list");
    for (int i = 0; i < count; ++i) {
        aether_paint_command_t command{};
        require(aether_display_list_get_command(list, i, &command) == AETHER_OK, "read paint command");
        out << command.tag << ':';
        switch (command.tag) {
            case AETHER_PAINT_FILL_ROUNDED_RECT: {
                const auto& d = command.data.fill_rounded_rect;
                rect(out, d.rect); radii(out, d.radii); color(out, d.color); break;
            }
            case AETHER_PAINT_DRAW_TEXT: {
                const auto& d = command.data.draw_text;
                out << std::quoted(d.text ? d.text : "") << std::quoted(d.font_family) << ';'
                    << d.font_size << ',' << d.font_weight << ',' << d.text_align << ',' << d.wrap << ';';
                rect(out, d.rect); color(out, d.color); break;
            }
            case AETHER_PAINT_BEGIN_PAINT_CHUNK:
            case AETHER_PAINT_END_PAINT_CHUNK: out << command.data.paint_chunk.style_node_id; break;
            case AETHER_PAINT_PUSH_CLIP:
                rect(out, command.data.push_clip.rect); radii(out, command.data.push_clip.radii); break;
            case AETHER_PAINT_POP_CLIP: break;
            default: require(false, "fixture emitted an unhandled paint command: " + std::to_string(command.tag));
        }
        out << '\n';
        aether_paint_command_free(&command);
    }
    aether_display_list_destroy(list);
    return out.str();
}
static void ordinary_dom() {
    Fixture f("<body><div id='a'><section id='b'><span id='c'>text</span></section>"
              "<i id='middle'></i><i id='last'></i></div><footer id='tail'></footer></body>",
              {"a", "b", "c", "middle", "last", "tail"});
    auto a = f.id("a"), b = f.id("b"), c = f.id("c"), middle = f.id("middle"), last = f.id("last"), tail = f.id("tail");
    auto text = aether_node_get_child(c, 0);
    require(text && aether_node_get_type(text) == AETHER_NODE_TEXT, "text fixture");
    for (int detached = 0; detached != 2; ++detached) {
        if (detached) f.remove(f.body, a);
        for (int repeat = 0; repeat != 3; ++repeat) {
            f.reject(a, a, false, "self append");
            f.reject(b, a, false, "direct ancestor append");
            f.reject(c, a, false, "deep ancestor append");
            f.reject(text, text, false, "text self append");
            f.reject(tail, c, true, "remove non-child");
        }
        if (detached) f.append(f.body, a);
    }
    for (bool remove : {false, true}) {
        f.reject(nullptr, a, remove, "null parent", AETHER_ERROR_NULL_ARG);
        f.reject(a, nullptr, remove, "null child", AETHER_ERROR_NULL_ARG);
        f.reject(nullptr, nullptr, remove, "both null", AETHER_ERROR_NULL_ARG);
    }
    f.append(a, c);
    require(children(a) == std::vector<aether_node_t>({b, middle, last, c}), "move descendant up");
    f.append(tail, c);
    require(children(tail) == std::vector<aether_node_t>({c}) && children(a) == std::vector<aether_node_t>({b, middle, last}),
            "move between branches");
    f.append(a, middle);
    require(children(a) == std::vector<aether_node_t>({b, last, middle}), "reinsert middle at end exactly once");
    const auto before = f.snapshot();
    f.append(a, middle);
    require(f.snapshot() == before, "reinsert last preserves snapshot");
    f.append(b, c); f.remove(f.body, a);
    require(f.id("a") == nullptr && f.id("b") == nullptr && f.id("c") == nullptr, "detached subtree not indexed");
    f.append(f.body, a);
    require(f.id("a") == a && f.id("b") == b && f.id("c") == c, "reinsert subtree preserves identities");
    Runtime runtime("body{margin:0} div,section,span,footer,i{display:block}");
    runtime.resolve(f);
    Layout layout(f, runtime);
    const auto shape = geometry(layout), paint = display_list(layout, runtime);
    f.reject(c, a, false, "reject with live consumers");
    require(geometry(layout) == shape && display_list(layout, runtime) == paint, "consumers unchanged after rejection");
}
static void synthetic_boundary() {
    Fixture f("<body><div id='host'>authored<span id='leaf'>leaf</span></div>"
              "<section id='target'></section><aether-anonymous-flex-item id='named'></aether-anonymous-flex-item></body>",
              {"host", "leaf", "target", "named"});
    auto host = f.id("host"), leaf = f.id("leaf"), target = f.id("target"), named = f.id("named");
    auto text = aether_node_get_child(host, 0);
    Runtime runtime("body{margin:0} #host{display:flex;width:300px;height:40px}"
                    "#host::before{content:'before'} #host::after{content:'after'}");
    runtime.resolve(f);
    {
        Layout first(f, runtime), second(f, runtime);
        std::vector<aether_node_t> wrappers, pseudos, generated_texts;
        for (auto* layout : {&first, &second}) for (auto box : layout->boxes()) {
            auto node = aether_layout_box_get_element(box);
            if (!node || std::find(f.authored.begin(), f.authored.end(), node) != f.authored.end()) continue;
            add_unique(f.known, node);
            if (aether_node_get_type(node) != AETHER_NODE_ELEMENT) continue;
            const auto tag = owned_string(aether_element_get_tag_name(node));
            if (tag == "aether-anonymous-flex-item") add_unique(wrappers, node);
            else if (aether_node_get_parent(node) == host) add_unique(pseudos, node);
        }
        require(wrappers.size() == 2 && wrappers[0] != wrappers[1], "two independently owned public wrappers");
        require(pseudos.size() == 2, "public before and after pseudo boxes");
        for (auto wrapper : wrappers) {
            require(children(wrapper) == std::vector<aether_node_t>({text}), "wrapper aliases authored text");
            require(aether_node_get_parent(wrapper) == host && aether_node_get_parent(text) == host,
                    "wrapper has intentionally nonreciprocal layout alias");
        }
        for (auto pseudo : pseudos) {
            auto content = aether_node_get_child(pseudo, 0);
            require(content && aether_node_get_type(content) == AETHER_NODE_TEXT && aether_node_get_parent(content) == pseudo,
                    "public generated text child");
            add_unique(generated_texts, content); add_unique(f.known, content);
        }
        const auto first_shape = geometry(first), second_shape = geometry(second);
        const auto first_paint = display_list(first, runtime), second_paint = display_list(second, runtime);
        const auto check_consumers = [&] {
            require(geometry(first) == first_shape && geometry(second) == second_shape, "unchanged geometry/text/box order");
            require(display_list(first, runtime) == first_paint && display_list(second, runtime) == second_paint,
                    "unchanged full fixture display list");
        };
        auto synthetic = wrappers;
        synthetic.insert(synthetic.end(), pseudos.begin(), pseudos.end());
        synthetic.insert(synthetic.end(), generated_texts.begin(), generated_texts.end());
        for (int repeat = 0; repeat != 3; ++repeat) {
            for (auto wrapper : wrappers) {
                f.reject(text, wrapper, false, "exact public wrapper/text cycle bypass"); check_consumers();
                f.reject(wrapper, text, true, "remove borrowed authored alias"); check_consumers();
            }
            for (auto node : synthetic) for (bool remove : {false, true}) {
                f.reject(node, leaf, remove, "synthetic parent"); check_consumers();
                f.reject(target, node, remove, "synthetic child"); check_consumers();
                f.reject(node, node, remove, "synthetic self"); check_consumers();
                f.reject(node, nullptr, remove, "null takes precedence over synthetic parent", AETHER_ERROR_NULL_ARG);
                f.reject(nullptr, node, remove, "null takes precedence over synthetic child", AETHER_ERROR_NULL_ARG);
            }
            for (auto pseudo : pseudos) {
                f.reject(host, pseudo, true, "remove generated root from actual parent"); check_consumers();
                f.reject(pseudo, aether_node_get_child(pseudo, 0), true, "remove unmarked generated text"); check_consumers();
            }
            f.reject(wrappers[0], wrappers[1], false, "cross-result wrappers"); check_consumers();
        }
        // A fresh layout and paint pass must also see the same unmodified DOM.
        { Layout fresh(f, runtime);
          require(geometry(fresh) == first_shape && display_list(fresh, runtime) == first_paint, "fresh consumers unchanged"); }
        // Provenance, not spelling or the presence of generated descendants.
        f.append(target, named); f.append(f.body, named);
        f.append(target, host); f.append(f.body, host);
        f.append(target, text); f.append(host, text);
        require(children(wrappers[0]) == std::vector<aether_node_t>({text}) &&
                children(wrappers[1]) == std::vector<aether_node_t>({text}) && aether_node_get_parent(text) == host,
                "authored alias stays mutable without adopting wrappers");
        f.remove(host, text); f.append(host, text);
        require(aether_node_get_parent(text) == host && f.id("host") == host && f.id("leaf") == leaf,
                "valid authored remove/reinsert and host mutation after rejections");
        // Prior layouts are snapshots; do not demand that valid mutations update
        // them, and never use their wrapper handles after releasing the results.
    }
    f.known = f.authored;
    runtime.resolve(f); // Old pseudo handles may be replaced here; none is reused.
    Layout final_layout(f, runtime);
    require(!geometry(final_layout).empty() && !display_list(final_layout, runtime).empty(), "fresh consumers after valid mutations");
}
int main(int argc, char** argv) {
    // Isolate the public wrapper regression when checking an unfixed baseline.
    const bool synthetic_only = argc == 2 && std::string(argv[1]) == "--synthetic-only";
    require(argc == 1 || synthetic_only, "usage: test_dom_mutation_integrity [--synthetic-only]");
    if (!synthetic_only) ordinary_dom();
    for (int cycle = 0; cycle != 3; ++cycle) synthetic_boundary();
    std::puts("PASS: public DOM mutation integrity, synthetic endpoints, consumers and lifetime ordering");
}
