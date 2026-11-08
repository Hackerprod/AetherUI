// Focal named-color integration through the public runtime ABI. Checks remain
// active under NDEBUG. Hex controls are independent of the named-color table.
#include <aether/aether.h>

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <utility>

namespace {
unsigned checks = 0, failures = 0;
void need(bool ok, const char* label) {
    if (!ok) { std::fprintf(stderr, "HARNESS FAIL: %s\n", label); std::exit(2); }
}
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}
bool same(float a, float b) { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b); }
bool color(aether_color_rgba a, aether_color_rgba b) {
    return same(a.r,b.r) && same(a.g,b.g) && same(a.b,b.b) && same(a.a,b.a);
}
bool length(aether_css_length a, aether_css_length b) {
    return a.unit == b.unit && same(a.value,b.value) && same(a.linear_percent,b.linear_percent) &&
        same(a.linear_em,b.linear_em) && same(a.linear_rem,b.linear_rem) &&
        same(a.linear_vw,b.linear_vw) && same(a.linear_vh,b.linear_vh);
}
void compare(const aether_computed_style_t& a, const aether_computed_style_t& b,
             const std::string& label) {
    check(color(a.color,b.color), label + " color");
    check(color(a.background_color,b.background_color), label + " background color");
    check(color(a.border_colors.top,b.border_colors.top) && color(a.border_colors.right,b.border_colors.right) &&
          color(a.border_colors.bottom,b.border_colors.bottom) && color(a.border_colors.left,b.border_colors.left), label + " all border colors");
    check(length(a.border_width.top,b.border_width.top) && length(a.border_width.right,b.border_width.right) &&
          length(a.border_width.bottom,b.border_width.bottom) && length(a.border_width.left,b.border_width.left), label + " all border widths");
    check(a.border_styles.top == b.border_styles.top && a.border_styles.right == b.border_styles.right &&
          a.border_styles.bottom == b.border_styles.bottom && a.border_styles.left == b.border_styles.left, label + " all border styles");
    check(aether_shadow_list_equal(&a.box_shadows,&b.box_shadows), label + " complete box shadows");
    check(aether_shadow_list_equal(&a.text_shadows,&b.text_shadows), label + " complete text shadows");
    // Includes every stop's color, position, offset and presence flag, as well as
    // layer order, size, position and repeat. Do not compare colors alone.
    check(aether_background_layer_list_equal(&a.background_layers,&b.background_layers), label + " complete background layers");
}
struct Case { const char* label; std::string actual, expected; };
const char* gradient = "linear-gradient(rebeccapurple 0%,lightgoldenrodyellow 100%)";
const char* hex_gradient = "linear-gradient(#663399 0%,#fafad2 100%)";
std::string reference_expected(const std::string& tone, bool shorthand, const std::string& explicit_tone = "#4b0082") {
    return "color:" + tone + ";" + (shorthand ? "background:currentColor;" : "background-color:currentColor;") +
        "border:3px solid " + tone + ";box-shadow:1px 2px " + tone + ",3px 4px " + tone + ",5px 6px " + explicit_tone +
        ";text-shadow:1px 2px " + tone + ",3px 4px " + tone + ",5px 6px " + explicit_tone + ";";
}
void run(bool baseline_controls) {
    const std::string g = gradient, h = hex_gradient;
    std::vector<Case> cases = {
        {"literal", "color:ReBeccAPuRple;", "color:#663399;"},
        {"inherited", "", "color:#663399;"},
        {"variable", "--tone:lightgoldenrodyellow;color:var(--tone);", "color:#fafad2;"},
        {"missing variable fallback", "color:var(--missing,indigo);", "color:#4b0082;"},
        {"background color", "background-color:papayawhip;", "background-color:#ffefd5;"},
        {"text fill alias", "text-fill-color:indigo;", "text-fill-color:#4b0082;"},
        {"webkit fill alias", "-webkit-text-fill-color:lightslategray;", "-webkit-text-fill-color:#778899;"},
        {"four border longhands", "border:3px dashed; border-top-color:indigo;border-right-color:coral;border-bottom-color:tomato;border-left-color:goldenrod;",
         "border:3px dashed;border-top-color:#4b0082;border-right-color:#ff7f50;border-bottom-color:#ff6347;border-left-color:#daa520;"},
        {"border shorthand", "border:4px dotted rebeccapurple;", "border:4px dotted #663399;"},
        {"border side shorthand", "border-left:5px solid darkslategrey;", "border-left:5px solid #2f4f4f;"},
        {"border colors four values", "border:2px solid;border-color:indigo coral tomato goldenrod;", "border:2px solid;border-color:#4b0082 #ff7f50 #ff6347 #daa520;"},
        {"border colors two values", "border:2px solid;border-color:slategray lightgrey;", "border:2px solid;border-color:#708090 #d3d3d3;"},
        {"literal invalid preserves fallback", "color:indigo;color:notacolor;", "color:#4b0082;"},
        {"computed invalid inherits, not var fallback", "--bad:notacolor;color:indigo;color:var(--bad,lime);", "color:#663399;"},
        {"color currentColor inherits", "color:currentColor;", "color:#663399;"},
        {"shadow named start end transparent", "box-shadow:indigo 1px 2px,3px 4px 5px rebeccapurple,6px 7px transparent;text-shadow:indigo 1px 2px,3px 4px rebeccapurple,6px 7px transparent;",
         "box-shadow:#4b0082 1px 2px,3px 4px 5px #663399,6px 7px transparent;text-shadow:#4b0082 1px 2px,3px 4px #663399,6px 7px transparent;"},
        {"shadow literal invalid list transactional", "box-shadow:1px 2px indigo;box-shadow:3px 4px coral,5px 6px notacolor;text-shadow:1px 2px indigo;text-shadow:3px 4px coral,5px 6px notacolor;",
         "box-shadow:1px 2px #4b0082;text-shadow:1px 2px #4b0082;"},
        {"shadow computed invalid list transactional", "--bad:notacolor;box-shadow:1px 2px indigo;box-shadow:3px 4px coral,5px 6px var(--bad,lime);text-shadow:1px 2px indigo;text-shadow:3px 4px coral,5px 6px var(--bad,lime);",
         "box-shadow:unset;text-shadow:unset;"},
        {"gradient image", "background-image:" + g + ";", "background-image:" + h + ";"},
        {"gradient shorthand final color", "background:" + g + " papayawhip;", "background:" + h + " #ffefd5;"},
        {"two layers final color", "background:" + g + "," + g + " indigo;", "background:" + h + "," + h + " #4b0082;"},
        {"color after slash", "background:" + g + " center / cover no-repeat rebeccapurple;", "background:" + h + " center / cover no-repeat #663399;"},
        {"first layer color rejected", "background:" + h + " #ffefd5;background:" + g + " indigo," + g + ";", "background:" + h + " #ffefd5;"},
        {"duplicate colors rejected", "background:#ffefd5;background:indigo coral;", "background:#ffefd5;"},
        {"invalid stop rejects whole gradient", "background-image:" + h + ";background-image:linear-gradient(indigo,notacolor);", "background-image:" + h + ";"},
        {"currentColor gradient remains unsupported", "background-image:" + h + ";background-image:linear-gradient(indigo,currentColor);", "background-image:" + h + ";"},
        {"currentColor compound remains unsupported", "background:#ffefd5;background:" + g + " currentColor;", "background:#ffefd5;"},
        // Compare the existing background currentColor routes against the same
        // routes with a concrete hex producer. This checks stability, not CSS
        // correctness or newly implemented contextual background support.
        {"references before producer", "background-color:currentColor;border:3px solid;box-shadow:1px 2px,3px 4px currentColor,5px 6px indigo;text-shadow:1px 2px,3px 4px currentColor,5px 6px indigo;color:rebeccapurple;",
         reference_expected("#663399", false)},
        {"references after producer and background fallback", "color:rebeccapurple;background:currentColor;border:3px solid currentColor;box-shadow:1px 2px,3px 4px currentColor,5px 6px indigo;text-shadow:1px 2px,3px 4px currentColor,5px 6px indigo;",
         reference_expected("#663399", true)}
    };
    if (baseline_controls) {
        cases.erase(cases.begin(), cases.end() - 2);
        for (auto& c : cases) {
            for (const auto& replacement : std::vector<std::pair<std::string,std::string>>{
                     {"rebeccapurple","red"}, {"indigo","blue"}, {"#663399","#ff0000"}, {"#4b0082","#0000ff"}}) {
                for (auto* value : {&c.actual,&c.expected}) {
                    size_t pos = 0;
                    while ((pos = value->find(replacement.first,pos)) != std::string::npos) {
                        value->replace(pos,replacement.first.size(),replacement.second);
                        pos += replacement.second.size();
                    }
                }
            }
        }
    }
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    need(aether_doc_create(&doc) == AETHER_OK, "create document");
    std::string html = "<html><body><div id='parent'>";
    std::string css = baseline_controls ? "#parent{color:red;text-shadow:7px 8px blue;}" :
        "#parent{color:rebeccapurple;text-shadow:7px 8px indigo;}";
    for (size_t i = 0; i < cases.size(); ++i) {
        const auto id = std::to_string(i);
        html += "<div id='a" + id + "'></div><div id='e" + id + "'></div>";
        css += "#a" + id + "{" + cases[i].actual + "}#e" + id + "{" + cases[i].expected + "}";
    }
    html += "</div></body></html>";
    need(aether_doc_parse_html(doc,html.c_str()) == AETHER_OK, "parse HTML");
    aether_style_runtime_options_t options{};
    options.max_variable_depth = 32;
    options.max_variable_expansion_length = 65536;
    aether_style_environment_t environment{800,600,1,16,0,0,0};
    need(aether_style_runtime_create(&options,&environment,nullptr,nullptr,&runtime) == AETHER_OK, "create runtime");
    int64_t sheet{};
    need(aether_style_runtime_add_stylesheet(runtime,css.c_str(),2,"named-colors.css",&sheet,nullptr) == AETHER_OK, "compile stylesheet");
    // A second pass on the same runtime changes the final producer. Named
    // explicit shadows remain indigo, while omitted/currentColor references move.
    for (int pass = 0; pass != 2; ++pass) {
        if (pass) {
            std::string override_css;
            for (size_t i = cases.size() - 2; i < cases.size(); ++i) {
                const auto id = std::to_string(i);
                override_css += "#a" + id + "{color:" + (baseline_controls ? std::string("#00ff00") : std::string("lime")) + ";}#e" + id + "{" + reference_expected("#00ff00",i == cases.size()-1,baseline_controls ? "#0000ff" : "#4b0082") + "}";
            }
            int64_t override_sheet{};
            need(aether_style_runtime_add_stylesheet(runtime,override_css.c_str(),2,"named-colors-update.css",&override_sheet,nullptr) == AETHER_OK, "update producer");
            aether_style_runtime_clear_cache(runtime);
        }
        std::vector<aether_computed_style_t> actual(cases.size()), expected(cases.size());
        std::vector<bool> seen_actual(cases.size()), seen_expected(cases.size());
        aether_style_update_t* updates{};
        const int count = aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);
        need(count > 0 && updates, "resolve subtree");
        for (int j = 0; j < count; ++j) {
            aether_node_t node{};
            aether_computed_style_handle_t handle{};
            need(aether_style_runtime_get_update(updates[j],&node,&handle,nullptr) == AETHER_OK, "get update");
            for (size_t i = 0; i < cases.size(); ++i) {
                const auto id = std::to_string(i);
                if (node == aether_doc_get_element_by_id(doc,("a"+id).c_str())) {
                    need(aether_computed_style_copy(handle,&actual[i]) == AETHER_OK, "copy actual"); seen_actual[i] = true;
                }
                if (node == aether_doc_get_element_by_id(doc,("e"+id).c_str())) {
                    need(aether_computed_style_copy(handle,&expected[i]) == AETHER_OK, "copy expected"); seen_expected[i] = true;
                }
            }
            aether_computed_style_handle_destroy(handle);
        }
        aether_style_updates_free(updates,count);
        for (size_t i = 0; i < cases.size(); ++i) {
            need(seen_actual[i] && seen_expected[i], "both target updates present");
            compare(actual[i],expected[i],std::string(cases[i].label) + " pass " + std::to_string(pass));
            // Prevent matching empty lists from disguising a rejected control.
            if (std::string(cases[i].label) == "gradient image") {
                check(actual[i].background_layers.count == 1, "named gradient layer present");
                if (actual[i].background_layers.count == 1) {
                    const auto& g2 = actual[i].background_layers.items[0].gradient;
                    check(g2.stop_count == 2, "named gradient has both stops");
                    if (g2.stop_count == 2) {
                        check(color(g2.stops[0].color,{102/255.f,51/255.f,153/255.f,1}) &&
                              color(g2.stops[1].color,{250/255.f,250/255.f,210/255.f,1}), "gradient independent RGBA bytes");
                        check(g2.stops[0].has_position && g2.stops[1].has_position &&
                              same(g2.stops[0].position.value,0.f) && same(g2.stops[1].position.value,100.f), "gradient explicit endpoint positions");
                    }
                }
            }
            aether_computed_style_free(&actual[i]);
            aether_computed_style_free(&expected[i]);
        }
    }
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(doc);
}
} // namespace
int main(int argc, char** argv) {
    const bool baseline_controls = argc == 2 && std::string(argv[1]) == "--baseline-controls";
    run(baseline_controls);
    std::printf("named_colors_runtime: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
