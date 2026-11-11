// Integration smoke/invariant test for the unmodified 403-example HTML page.
// This is NOT a 403-case CSS conformance suite or a browser/Direct2D renderer.
#include <aether/aether.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

using Clock = std::chrono::steady_clock;
static std::ofstream report;
static int checks = 0, failures = 0;
static void check(bool ok, const std::string& name) {
    ++checks;
    if (!ok) { ++failures; report << "FAIL " << name << '\n'; std::cerr << "FAIL " << name << '\n'; }
}
struct CheckFailure : std::runtime_error { using std::runtime_error::runtime_error; };
static void require(bool ok, const char* name) {
    check(ok, name);
    if (!ok) throw CheckFailure(name);
}
static std::string take(char* s) {
    std::string result = s ? s : "";
    aether_string_free(s);
    return result;
}
static std::string attr(aether_node_t n, const char* name) {
    return take(aether_element_get_attribute(n, name));
}
static bool has_class(aether_node_t n, const char* value) {
    std::istringstream words(attr(n, "class"));
    for (std::string word; words >> word;) if (word == value) return true;
    return false;
}
static void nodes(aether_node_t n, std::vector<aether_node_t>& out) {
    if (!n) return;
    out.push_back(n);
    for (int i = 0; i < aether_node_get_child_count(n); ++i)
        nodes(aether_node_get_child(n, i), out);
}
// Deliberately deterministic approximate metrics, not native font shaping.
static aether_text_metrics_t measure(const char* text, const aether_computed_style_t* style,
                                     float available, void*) {
    const float size = style && style->font_size > 0 ? style->font_size : 16.f;
    size_t glyphs = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); p && *p; ++p)
        if ((*p & 0xc0) != 0x80) ++glyphs;
    const float natural = glyphs * size * .5f;
    const float width = std::isfinite(available) && available > 0 ? std::min(natural, available) : natural;
    const float lines = width > 0 ? std::max(1.f, std::ceil(natural / width)) : 1.f;
    return {width, size * 1.2f * lines, size * .9f};
}
struct Pipeline {
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    aether_style_map_t styles{};
    aether_layout_engine_t engine{};
    aether_layout_result_t layout{};
    aether_display_list_builder_t builder{};
    aether_display_list_t list{};
    aether_style_update_t* updates{};
    int32_t update_count{};
    ~Pipeline() {
        aether_display_list_destroy(list); aether_display_list_builder_destroy(builder);
        aether_layout_result_destroy(layout); aether_layout_destroy(engine);
        aether_style_map_destroy(styles); aether_style_updates_free(updates, update_count);
        aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    }
};
static bool finite_rect(aether_rectf r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) &&
           std::isfinite(r.height) && r.width >= 0 && r.height >= 0;
}
static std::string csv(const std::string& s) {
    std::string result = "\"";
    for (char c : s) { if (c == '"') result += '"'; result += c; }
    return result + '"';
}
static void boxes(aether_layout_box_t box, int& count) {
    if (!box) return;
    check(finite_rect(aether_layout_box_get_border_box(box)), "finite nonnegative box " + std::to_string(count++));
    for (int i = 0; i < aether_layout_box_get_child_count(box); ++i)
        boxes(aether_layout_box_get_child(box, i), count);
}
static void axis_case(Pipeline& p, const char* id, bool column, bool reverse) {
    auto article = aether_doc_get_element_by_id(p.doc, id);
    std::vector<aether_node_t> descendants; nodes(article, descendants);
    aether_node_t target{};
    for (auto n : descendants) if (has_class(n, "flex-target")) { target = n; break; }
    require(target != nullptr, "axis case target present");
    auto handle = aether_style_map_get(p.styles, target);
    require(handle != nullptr, "axis target has computed style");
    aether_computed_style_t style{};
    const auto copied = aether_computed_style_copy(handle, &style);
    aether_computed_style_handle_destroy(handle);
    require(copied == AETHER_OK, "axis target style readable");
    check(style.display == AETHER_DISPLAY_FLEX, std::string(id) + " computed display flex");
    if (std::string(id) != "FLEX-001")
        check(std::fabs(style.gap - 8.f) < .02f, std::string(id) + " computed gap 8");
    aether_computed_style_free(&style);
    std::vector<aether_rectf> items;
    for (int i = 0; i < aether_node_get_child_count(target); ++i) {
        auto n = aether_node_get_child(target, i);
        if (!has_class(n, "item")) continue;
        auto box = aether_layout_result_find_primary_box(p.layout, n);
        require(box != nullptr, "axis item has box");
        auto item = aether_layout_box_get_border_box(box);
        check(item.width >= 42.f - .02f && item.height >= 36.f - .02f,
              std::string(id) + " item respects positive fixture minimums");
        items.push_back(item);
    }
    require(items.size() == 3, "axis case has three items");
    for (size_t i = 0; i < items.size(); ++i) {
        auto r = items[i];
        report << "case_item=" << id << "," << i << "," << r.x << "," << r.y << "," << r.width << "," << r.height << '\n';
    }
    auto box = aether_layout_result_find_primary_box(p.layout, target);
    require(box != nullptr, "axis target has box");
    check(std::fabs(aether_layout_box_get_border_box(box).width - 420.f) < .02f,
          std::string(id) + " declared border-box width 420");
    for (size_t i = 1; i < items.size(); ++i) {
        auto a = items[i-1], b = items[i];
        const float pos_a = column ? a.y : a.x, pos_b = column ? b.y : b.x;
        const float size_a = column ? a.height : a.width, size_b = column ? b.height : b.width;
        check(reverse ? pos_b + size_b <= pos_a + .02f : pos_a + size_a <= pos_b + .02f,
              std::string(id) + " ordered non-overlapping items");
        if (std::string(id) != "FLEX-001")
            check(std::fabs(std::max(pos_a, pos_b) - std::min(pos_a + size_a, pos_b + size_b) - 8.f) < .05f,
                  std::string(id) + " used gap 8");
        check(std::fabs((column ? a.x : a.y) - (column ? b.x : b.y)) < .02f,
              std::string(id) + " common cross-axis origin");
    }
    report << "semantic_assertions_exercised=" << id << '\n';
}
int main(int argc, char** argv) {
    if (argc != 5) { std::cerr << "usage: test HTML OUTPUT_DIRECTORY WIDTH HEIGHT\n"; return 2; }
    try {
        const float width = std::stof(argv[3]), height = std::stof(argv[4]);
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
            throw std::invalid_argument("viewport must be finite and positive");
        std::filesystem::create_directories(argv[2]);
        report.open(std::filesystem::path(argv[2]) / "report.log");
        if (!report) throw std::runtime_error("cannot open report.log");
        report << std::unitbuf;
        report << "scope=native page pipeline + structural invariants + five selected semantic cases\n"
               << "not_validated=398 remaining semantic cases; browser parity; raster pixels; Direct2D; actual fonts; image decode\n"
               << "text_metrics=synthetic UTF-8 codepoint widths, approximate wrapping\nviewport=" << width << 'x' << height << '\n';
        std::ifstream input(argv[1], std::ios::binary);
        require(bool(input), "fixture readable");
        std::string html((std::istreambuf_iterator<char>(input)), {});
        report << "fixture_bytes=" << html.size() << '\n';
        Pipeline p;
        auto time = Clock::now();
        auto timing = [&](const char* name) {
            const auto now = Clock::now();
            report << name << "_ms=" << std::chrono::duration<double, std::milli>(now-time).count() << '\n';
            time = now;
        };
        require(aether_doc_create(&p.doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(p.doc, html.c_str()) == AETHER_OK, "parse entire HTML");
        timing("parse");
        std::vector<aether_node_t> all; nodes(aether_doc_get_document_element(p.doc), all);
        std::string css;
        int articles = 0, sheets = 0;
        for (auto n : all) {
            if (take(aether_element_get_tag_name(n)) == "style") {
                std::vector<aether_node_t> style_nodes; nodes(n, style_nodes);
                for (auto child : style_nodes) css += take(aether_node_get_text_content(child));
                css += '\n'; ++sheets;
            }
            if (!attr(n, "data-test-id").empty()) ++articles;
        }
        require(articles == 403, "403 case articles parsed (inventory, not semantic PASS)");
        require(sheets > 0 && !css.empty(), "embedded stylesheet extracted from DOM");
        report << "dom_nodes=" << all.size() << "\ncase_inventory=" << articles << "\nstyle_elements=" << sheets << '\n';
        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{width, height, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &p.runtime) == AETHER_OK, "create runtime");
        int64_t sheet{}; int32_t rules{};
        require(aether_style_runtime_add_stylesheet(p.runtime, aether_style_runtime_defaults_css(), 0, "aether-defaults.css", &sheet, &rules) == AETHER_OK, "load engine UA defaults");
        require(aether_style_runtime_add_stylesheet(p.runtime, css.c_str(), 2, "flexbox-page", &sheet, &rules) == AETHER_OK, "compile page CSS");
        require(rules > 0, "nonempty compiled page CSS");
        p.update_count = aether_style_runtime_resolve_subtree(p.runtime, aether_doc_get_document_element(p.doc), &p.updates);
        require(p.update_count > 0, "resolve page styles");
        require(aether_style_runtime_create_resolved_style_map(p.runtime, p.doc, &p.styles) == AETHER_OK, "resolved map");
        report << "css_rules=" << rules << "\nstyle_updates=" << p.update_count << '\n';
        timing("css_resolve");
        require(aether_layout_create(measure, nullptr, &p.engine) == AETHER_OK, "create layout");
        require(aether_layout_layout(p.engine, p.doc, p.styles, width, height, nullptr, nullptr, &p.layout) == AETHER_OK, "layout entire page");
        timing("layout");
        int count = 0; boxes(aether_layout_result_get_root(p.layout), count);
        require(count > 403, "nontrivial page layout tree");
        report << "layout_boxes=" << count << '\n';
        std::ofstream geometry(std::filesystem::path(argv[2]) / "geometry.csv");
        require(bool(geometry), "geometry output readable");
        geometry << "dom_index,case_id,tag,id,class,x,y,width,height\n";
        for (size_t i = 0; i < all.size(); ++i) {
            auto n = all[i];
            auto box = aether_layout_result_find_primary_box(p.layout, n);
            if (!box) continue;
            std::string case_id;
            for (auto parent = n; parent; parent = aether_node_get_parent(parent)) {
                case_id = attr(parent, "data-test-id"); if (!case_id.empty()) break;
            }
            auto r = aether_layout_box_get_border_box(box);
            geometry << i << ',' << csv(case_id) << ',' << csv(take(aether_element_get_tag_name(n))) << ','
                     << csv(attr(n,"id")) << ',' << csv(attr(n,"class")) << ',' << r.x << ',' << r.y << ',' << r.width << ',' << r.height << '\n';
        }
        for (int i = 1; i <= 403; ++i) {
            char id[20]; std::snprintf(id, sizeof(id), "FLEX-%03d", i);
            auto n = aether_doc_get_element_by_id(p.doc, id);
            check(n != nullptr, std::string(id) + " exists");
            check(n && attr(n, "data-test-id") == id, std::string(id) + " matches inventory attribute");
            auto box = aether_layout_result_find_primary_box(p.layout, n);
            check(box && finite_rect(aether_layout_box_get_border_box(box)), std::string(id) + " finite article geometry only");
        }
        axis_case(p, "FLEX-001", false, false);
        axis_case(p, "FLEX-022", false, false); axis_case(p, "FLEX-023", false, true);
        axis_case(p, "FLEX-024", true, false); axis_case(p, "FLEX-025", true, true);
        timing("invariants_export");
        require(aether_display_list_builder_create(measure, nullptr, &p.builder) == AETHER_OK, "create painter");
        require(aether_display_list_build(p.builder, p.layout, nullptr, &p.list) == AETHER_OK, "build display list");
        const int commands = aether_display_list_get_count(p.list);
        require(commands > 0, "nonempty display list");
        for (int i = 0; i < commands; ++i) {
            aether_paint_command_t command{};
            check(aether_display_list_get_command(p.list, i, &command) == AETHER_OK, "display command accessible " + std::to_string(i));
            aether_paint_command_free(&command);
        }
        report << "display_commands=" << commands << '\n';
        timing("display_list");
        geometry.flush(); check(bool(geometry), "geometry written successfully");
    } catch (const CheckFailure& e) {
        report << "ERROR " << e.what() << '\n'; std::cerr << e.what() << '\n';
    } catch (const std::exception& e) {
        ++failures; report << "ERROR " << e.what() << '\n'; std::cerr << e.what() << '\n';
    }
    report << "assertions=" << checks << "\nfailures=" << failures << "\nresult=" << (failures ? "FAIL" : "PASS (limited scope)") << '\n';
    report.flush();
    return failures || !report ? 1 : 0;
}
