// Public ABI regression for terminal token-based CSS priority recognition.
// Keep require active in Release/NDEBUG: this suite must not become a no-op.
#include <aether/aether.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
int checks = 0;
std::string context;
void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL [%s]: %s\n", context.c_str(), message.c_str());
        std::exit(1);
    }
}
struct snapshot {
    aether_computed_style_t value{};
    snapshot() = default;
    snapshot(const snapshot&) = delete;
    snapshot& operator=(const snapshot&) = delete;
    ~snapshot() { aether_computed_style_free(&value); }
};
struct fixture {
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    aether_node_t target{};
    aether_computed_style_handle_t current{};
    std::string generated_text;
    float generated_width = -1;
    fixture() {
        require(aether_doc_create(&doc) == AETHER_OK, "document creation");
        require(aether_doc_parse_html(doc,
            "<body><div id='parent'><div id='target'></div></div></body>") == AETHER_OK, "HTML parse");
        target = aether_doc_get_element_by_id(doc, "target");
        require(target != nullptr, "target lookup");
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK,
                "runtime creation");
    }
    ~fixture() {
        aether_computed_style_handle_destroy(current);
        aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    }
    int64_t sheet(const std::string& css) {
        int64_t id = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 2, "important.css", &id, nullptr)
                == AETHER_OK, "stylesheet compilation");
        return id;
    }
    void inline_style(const std::string& css) {
        require(aether_element_set_attribute(target, "style", css.c_str()) == AETHER_OK, "set inline style");
    }
    void declarations(const std::string& css, bool inlined) {
        if (inlined) inline_style(css); else sheet("#target{" + css + "}");
    }
    void resolve() {
        generated_text.clear(); generated_width = -1;
        aether_style_update_t* updates = nullptr;
        const int count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
        require(count >= 0, "resolve");
        for (int i = 0; i < count; ++i) {
            aether_node_t node{};
            aether_computed_style_handle_t handle{};
            require(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr) == AETHER_OK,
                    "update access");
            if (node != target && aether_node_get_parent(node) == target) {
                snapshot pseudo;
                require(aether_computed_style_copy(handle, &pseudo.value) == AETHER_OK, "pseudo copy");
                if (pseudo.value.width.unit == AETHER_CSS_UNIT_PX) generated_width = pseudo.value.width.value;
                std::vector<aether_node_t> pending{node};
                while (!pending.empty()) {
                    const auto current = pending.back(); pending.pop_back();
                    char* text = aether_node_get_text_content(current);
                    if (text) generated_text += text;
                    aether_string_free(text);
                    for (int j = aether_node_get_child_count(current); j > 0; --j)
                        pending.push_back(aether_node_get_child(current, j - 1));
                }
            }
            if (node == target) {
                aether_computed_style_handle_destroy(current);
                current = handle;
            } else aether_computed_style_handle_destroy(handle);
        }
        aether_style_updates_free(updates, count);
    }
    void copy(snapshot& out) {
        // get_update returns an owned snapshot handle. Keep the latest target
        // update when a repeated resolve correctly emits no changed target.
        require(current != nullptr, "target update handle");
        require(aether_computed_style_copy(current, &out.value) == AETHER_OK, "snapshot copy");
    }
};
void px(aether_css_length actual, float expected, const char* label) {
    require(actual.unit == AETHER_CSS_UNIT_PX && actual.value == expected,
            std::string(label) + ": expected " + std::to_string(expected) + ", actual " +
            std::to_string(actual.value) + " unit " + std::to_string(actual.unit));
}
void width(fixture& f, float expected) {
    f.resolve(); snapshot s; f.copy(s); px(s.value.width, expected, "width");
}
void margins(const aether_computed_style_t& s, float top, float right, float bottom, float left) {
    px(s.margin.top, top, "margin-top"); px(s.margin.right, right, "margin-right");
    px(s.margin.bottom, bottom, "margin-bottom"); px(s.margin.left, left, "margin-left");
}
struct example { const char* label; const char* marker; bool valid; };
void lexical_matrix() {
    // First sixteen entries reproduce the original 96 ABI observations exactly.
    const example examples[] = {
        {"canonical", "!important", true}, {"mixed case", "!ImPoRtAnT", true},
        {"space", "! important", true}, {"tab", "!\timportant", true},
        {"LF", "!\nimportant", true}, {"FF", "!\fimportant", true},
        {"CR", "!\rimportant", true}, {"CRLF", "!\r\nimportant", true},
        {"comment", "!/**/important", true}, {"comment and space", "! /*priority*/ important", true},
        {"trailing comment", "!important/**/", true}, {"escaped identifier", "!\\69mportant", true},
        {"simple escape", "!\\important", true}, {"identifier suffix", "!importantx", false},
        {"identifier split", "!im/**/portant", false}, {"missing bang", " important", false},
        {"six digit escape", "!\\000069mportant", true}, {"hex terminator space", "!\\69 mportant", true},
        {"internal escape", "!impor\\74 ant", true}, {"terminal escape", "!importan\\74 ", true},
        {"hex terminator tab", "!\\69\tmportant", true}, {"hex terminator LF", "!\\69\nmportant", true},
        {"hex terminator FF", "!\\69\fmportant", true}, {"hex terminator CR", "!\\69\rmportant", true},
        {"hex terminator CRLF", "!\\69\r\nmportant", true},
        {"trailing tab", "!important\t", true}, {"trailing LF", "!important\n", true},
        {"trailing FF", "!important\f", true}, {"trailing CR", "!important\r", true},
        {"trailing CRLF", "!important\r\n", true},
        {"mixed trivia", "!\t/**/\r\n /*x*/\fImPoRtAnT/**/\t\f", true},
        {"hyphen suffix", "!important-", false}, {"function token", "!important()", false},
        {"trailing dimension", "!important 3px", false}, {"trailing bang", "!important !", false},
        {"hash", "!#important", false}, {"at keyword", "!@important", false},
        {"dimension", "!1important", false}, {"split spaces", "!im portant", false},
        {"double terminator", "!\\69  mportant", false}, {"split hex comment", "!\\69/**/mportant", false},
        {"escaped bang", "\\!important", false}, {"hex bang", "\\21 important", false},
        {"escaped trailing space", "!\\69mportant\\ ", false}, {"zero escape", "!\\0 mportant", false},
        {"surrogate escape", "!\\d800 mportant", false}, {"out of range escape", "!\\110000 mportant", false},
        {"non ASCII", "!\\130 mportant", false}, {"vertical tab", "!\vimportant", false},
        {"NBSP", "!\xC2\xA0important", false}, {"escaped newline", "!\\\nimportant", false},
        {"string", " '!important'", false}, {"URL", " url(!important)", false},
        {"function", " f(!important)", false}, {"square block", " [!important]", false},
        {"curly block", " {!important}", false}, {"following block", "!important []", false},
        {"literal comment in string", " '/* !important */'", false},
        {"escaped quote in string", " 'a\\\'!important'", false},
    };
    for (const auto& e : examples) for (bool inlined : {false, true}) for (int mode = 0; mode < 3; ++mode) {
        context = std::string(e.label) + (inlined ? " inline " : " sheet ") + std::to_string(mode);
        fixture f;
        std::string css;
        if (mode == 0) css = std::string("width:37px") + e.marker + ";width:91px";
        if (mode == 1) css = std::string("margin:37px") + e.marker + ";margin:91px";
        if (mode == 2) css = std::string("--size:37px") + e.marker + ";--size:91px;width:var(--size)";
        f.declarations(css, inlined); f.resolve(); snapshot s; f.copy(s);
        const float expected = e.valid ? 37 : 91;
        if (mode == 1) margins(s.value, expected, expected, expected, expected);
        else px(s.value.width, expected, "width");
    }
}
void consumers() {
    for (bool inlined : {false, true}) {
        context = inlined ? "inline consumers" : "stylesheet consumers";
        {
            fixture f; f.declarations("--size:37px;width:var(--size)! /**/important;width:91px", inlined);
            width(f, 37);
        }
        {
            // Explicit inherit on non-inherited width is not implemented. Use
            // inherited font-size to observe keyword priority through the ABI.
            fixture f; f.sheet("#parent{font-size:53px}");
            f.declarations("font-size:inherit! \\69mportant;font-size:91px", inlined);
            f.resolve(); snapshot s; f.copy(s);
            require(s.value.font_size == 53, "inherited keyword keeps priority");
        }
        for (const char* keyword : {"initial", "unset"}) {
            fixture f; f.declarations(std::string("width:") + keyword + "! important;width:91px", inlined);
            f.resolve(); snapshot s; f.copy(s);
            const auto defaults = aether_computed_style_default();
            require(s.value.width.unit == defaults.width.unit && s.value.width.value == defaults.width.value,
                    std::string(keyword) + " keeps priority");
        }
        {
            fixture f; f.declarations("margin:11px 22px 33px 44px! important;margin-left:91px;margin:99px", inlined);
            f.resolve(); snapshot s; f.copy(s); margins(s.value, 11, 22, 33, 44);
        }
        {
            fixture f; f.declarations("--empty:! important;--empty:91px;width:var(--empty)37px", inlined);
            width(f, 37); // Empty important custom value must remain a defined winner.
        }
        {
            fixture f; f.declarations("--size:37px! important;width:var(--size);width:91px", inlined);
            width(f, 91); // Custom priority must not leak into its normal consumer.
        }
        {
            fixture f; f.declarations("width:37px! important;width:var(--missing,91px !important)", inlined);
            width(f, 37); // A marker inside var() is not a declaration annotation.
        }
    }
    context = "deferred margin stylesheet";
    {
        fixture f; f.sheet("#target{--gap:11px 22px 33px 44px;margin:var(--gap)! important;margin-left:91px;margin:99px}");
        f.resolve(); snapshot s; f.copy(s); margins(s.value, 11, 22, 33, 44);
    }
    context = "custom inheritance";
    {
        fixture f; f.sheet("#parent{--size:37px! important;--size:91px}#target{width:var(--size)}");
        width(f, 37);
        f.inline_style("--size:53px;width:var(--size)"); width(f, 53);
        f.inline_style("--size:63px! important;--size:91px;width:var(--size)"); width(f, 63);
    }
    for (bool variable : {false, true}) {
        context = variable ? "pseudo var content" : "pseudo literal content";
        fixture f;
        f.sheet("#target{--label:'MiXeD /*literal*/ !important'! important;--label:'wrong'}"
                "#target::before{content:" + std::string(variable ? "var(--label)" : "'MiXeD /*literal*/ !important'") +
                "! important;content:'later';width:37px! \\69mportant;width:91px}");
        for (int i = 0; i < 3; ++i) {
            f.resolve(); require(f.generated_text == "MiXeD /*literal*/ !important", "generated literal text");
            require(f.generated_width == 37, "generated pseudo style");
        }
    }
}
void cascade_and_updates() {
    const char* cases[] = {
        "width:37px! important;width:91px",
        "width:91px;width:37px! important",
        "width:91px!important;width:37px! \\69mportant"
    };
    for (bool inlined : {false, true}) for (const char* css : cases) {
        context = "declaration order"; fixture f; f.declarations(css, inlined); width(f, 37);
    }
    context = "selector and inline cascade";
    {
        fixture f; f.sheet("div{width:37px! important}#parent #target{width:91px}"); width(f, 37);
        f.inline_style("width:53px"); width(f, 37);
        f.inline_style("width:63px! important"); width(f, 63);
    }
    context = "mutation, repeated resolution, and owned snapshots";
    snapshot retained;
    {
        fixture f;
        f.sheet("#target{width:91px}");
        auto sheet = f.sheet("#target{width:37px! important;background-image:url('asset://original')! important}");
        width(f, 37); f.copy(retained);
        for (int i = 0; i < 4; ++i) width(f, 37);
        require(aether_style_runtime_remove_stylesheet(f.runtime, sheet) != 0, "remove important sheet");
        width(f, 91);
        sheet = f.sheet("#target{width:37px!/**/\\69mportant}"); width(f, 37);
        f.inline_style("width:53px!important"); width(f, 53);
        f.inline_style("width:53px! /*same*/ \\000069mportant"); width(f, 53);
        f.inline_style("width:63px! important"); width(f, 63);
        f.inline_style("width:71px"); width(f, 37);
        require(aether_style_runtime_remove_stylesheet(f.runtime, sheet) != 0, "remove replacement sheet");
        width(f, 71);
        px(retained.value.width, 37, "retained snapshot after mutations");
    }
    px(retained.value.width, 37, "retained snapshot after runtime destruction");
    require(retained.value.background_layers.count == 1 && retained.value.background_layers.items &&
            retained.value.background_layers.items[0].image_source &&
            std::strcmp(retained.value.background_layers.items[0].image_source, "asset://original") == 0,
            "owned snapshot retains dynamic image storage");
}
} // namespace
int main() {
    lexical_matrix(); consumers(); cascade_and_updates();
    std::printf("PASS: important public ABI (%d checks, including original 96 observations)\n", checks);
    return 0;
}
