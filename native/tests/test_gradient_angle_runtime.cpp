// Public-ABI boundary/cascade regression. All checks remain active with NDEBUG.
#include <aether/aether.h>

#include <cmath>
#include <clocale>
#include <memory>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
size_t checks = 0;
size_t cases = 0;
std::string context;
void require(bool condition, const char* detail) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL [%s]: %s (check %zu)\n", context.c_str(), detail, checks);
        std::exit(1);
    }
}
void scalar(float a, float b, const char* field) {
    require(std::isfinite(a) && std::isfinite(b) && std::fabs(a - b) < .0001f, field);
}
void color(const aether_color_rgba& a, const aether_color_rgba& b) {
    scalar(a.r, b.r, "color.r"); scalar(a.g, b.g, "color.g");
    scalar(a.b, b.b, "color.b"); scalar(a.a, b.a, "color.a");
}
void length(const aether_css_length& a, const aether_css_length& b) {
    require(a.unit == b.unit, "length.unit");
    scalar(a.value, b.value, "length.value");
    scalar(a.linear_percent, b.linear_percent, "length.linear_percent");
    scalar(a.linear_em, b.linear_em, "length.linear_em");
    scalar(a.linear_rem, b.linear_rem, "length.linear_rem");
    scalar(a.linear_vw, b.linear_vw, "length.linear_vw");
    scalar(a.linear_vh, b.linear_vh, "length.linear_vh");
}
void gradient(const aether_linear_gradient& a, const aether_linear_gradient& b) {
    scalar(a.direction_x, b.direction_x, "gradient.direction_x");
    scalar(a.direction_y, b.direction_y, "gradient.direction_y");
    require(a.stop_count == b.stop_count, "gradient.stop_count");
    require((a.stops != nullptr) == (b.stops != nullptr), "gradient.stops presence");
    for (int32_t i = 0; i < a.stop_count; ++i) {
        color(a.stops[i].color, b.stops[i].color);
        length(a.stops[i].position, b.stops[i].position);
        scalar(a.stops[i].offset, b.stops[i].offset, "stop.offset");
        require(a.stops[i].has_position == b.stops[i].has_position, "stop.has_position");
    }
}
void snapshot(const aether_computed_style_t& a, const aether_computed_style_t& b) {
    color(a.background_color, b.background_color);
    require(a.background_layers.count == b.background_layers.count, "background layer count");
    require((a.background_layers.items != nullptr) == (b.background_layers.items != nullptr), "layer storage presence");
    for (int32_t i = 0; i < a.background_layers.count; ++i) {
        const auto& x = a.background_layers.items[i];
        const auto& y = b.background_layers.items[i];
        require(x.kind == y.kind, "layer.kind");
        require((x.image_source != nullptr) == (y.image_source != nullptr), "URL presence");
        if (x.image_source && y.image_source)
            require(std::strcmp(x.image_source, y.image_source) == 0, "URL contents");
        gradient(x.gradient, y.gradient);
        length(x.position_x, y.position_x); length(x.position_y, y.position_y);
        require(x.size_kind == y.size_kind, "layer.size_kind");
        length(x.size_x, y.size_x); length(x.size_y, y.size_y);
        require(x.repeat_x == y.repeat_x, "layer.repeat_x");
        require(x.repeat_y == y.repeat_y, "layer.repeat_y");
    }
    require(a.has_text_gradient == b.has_text_gradient, "has_text_gradient");
    gradient(a.text_gradient, b.text_gradient);
}
struct style {
    aether_computed_style_t value{};
    style() = default;
    style(const style&) = delete;
    style& operator=(const style&) = delete;
    ~style() { aether_computed_style_free(&value); }
};
struct fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_node_t body = nullptr;
    int64_t sheet = 0;
    aether_computed_style_handle_t current = nullptr;
    fixture() {
        require(aether_doc_create(&doc) == AETHER_OK, "doc create");
        require(aether_doc_parse_html(doc, "<html><body>angle</body></html>") == AETHER_OK, "doc parse");
        body = aether_doc_get_body(doc);
        require(body != nullptr, "body lookup");
        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{800, 600, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
                "runtime create");
    }
    ~fixture() {
        aether_computed_style_handle_destroy(current);
        aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    }
    void set(const std::string& declarations, bool inlined) {
        if (sheet) {
            require(aether_style_runtime_remove_stylesheet(runtime, sheet), "remove old sheet");
            sheet = 0;
        }
        require(aether_element_set_attribute(body, "style", inlined ? ("display:block;width:100px;height:80px;" + declarations).c_str() : "") == AETHER_OK,
                "inline mutation");
        if (!inlined) {
            const std::string source = "body {display:block;width:100px;height:80px;" + declarations + "}";
            require(aether_style_runtime_add_stylesheet(runtime, source.c_str(), 0,
                "gradient-boundary.css", &sheet, nullptr) == AETHER_OK, "add sheet");
        }
    }
    int copy(style& destination) {
        aether_style_update_t* updates = nullptr;
        const int32_t count = aether_style_runtime_resolve_subtree(runtime,
            aether_doc_get_document_element(doc), &updates);
        require(count >= 0, "resolve subtree");
        for (int32_t i = 0; i < count; ++i) {
            aether_node_t node = nullptr;
            require(aether_style_runtime_get_update(updates[i], &node, nullptr, nullptr) == AETHER_OK,
                    "inspect update node");
            if (node == body) {
                aether_computed_style_handle_t next = nullptr;
                require(aether_style_runtime_get_update(updates[i], nullptr, &next, nullptr) == AETHER_OK,
                        "get body style handle");
                aether_computed_style_handle_destroy(current);
                current = next;
            }
        }
        aether_style_updates_free(updates, count);
        require(current != nullptr, "computed handle");
        auto handle = current;
        style twin;
        require(aether_computed_style_copy(handle, &destination.value) == AETHER_OK, "copy computed style");
        require(aether_computed_style_copy(handle, &twin.value) == AETHER_OK, "second independent copy");
        snapshot(destination.value, twin.value);
        if (destination.value.background_layers.count) {
            require(destination.value.background_layers.items != twin.value.background_layers.items,
                    "layer copies must not alias");
            for (int32_t i = 0; i < destination.value.background_layers.count; ++i) {
                const auto& x = destination.value.background_layers.items[i];
                const auto& y = twin.value.background_layers.items[i];
                if (x.gradient.stop_count) require(x.gradient.stops != y.gradient.stops, "stop copies must not alias");
                if (x.image_source) require(x.image_source != y.image_source, "URL copies must not alias");
            }
        }
        if (destination.value.has_text_gradient)
            require(destination.value.text_gradient.stops != twin.value.text_gradient.stops,
                    "text gradient copies must not alias");
        // destination survives update destruction and, for reference styles,
        // handle/runtime/document destruction. No private structures are used.
        // On an unchanged resolve there need not be a new update: current owns
        // the most recent public-ABI handle, independently of the runtime.
        return count;
    }
};
const std::string lower_image = "linear-gradient(90deg,#e13c72 13%,#46218b calc(80% + 3px)),url(lower.png)";
const std::string lower =
    "background-color:#192b4d; background-image:" + lower_image + ";"
    "background-position:17% 23px,31px 47%;"
    "background-size:61px 73%,contain; background-repeat:space round,no-repeat repeat;";
const std::string fallback = "linear-gradient(180deg,#00aa00 7px,#003300 91%)";

// Owned public-ABI commands outlive the list, builder, layout and runtime.
struct command_deleter {
    void operator()(aether_paint_command_t* p) const { aether_paint_command_free(p); delete p; }
};
struct painting {
    std::vector<std::unique_ptr<aether_paint_command_t, command_deleter>> commands;
    int gradients = 0;
};
aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void*) {
    return {text ? static_cast<float>(std::strlen(text)) : 0.f, 1.f, .8f};
}
void paint(fixture& f, painting& out) {
    aether_style_map_t map = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t layout = nullptr;
    aether_display_list_builder_t builder = nullptr;
    aether_display_list_t list = nullptr;
    require(aether_style_runtime_create_resolved_style_map(f.runtime, f.doc, &map) == AETHER_OK, "style map");
    require(aether_layout_create(measure, nullptr, &engine) == AETHER_OK, "layout engine");
    require(aether_layout_layout(engine, f.doc, map, 800, 600, nullptr, nullptr, &layout) == AETHER_OK, "layout");
    require(aether_display_list_builder_create(measure, nullptr, &builder) == AETHER_OK, "paint builder");
    require(aether_display_list_build(builder, layout, nullptr, &list) == AETHER_OK, "display list");
    for (int i = 0; i < aether_display_list_get_count(list); ++i) {
        std::unique_ptr<aether_paint_command_t, command_deleter> command(new aether_paint_command_t{});
        require(aether_display_list_get_command(list, i, command.get()) == AETHER_OK, "copy paint command");
        if (command->tag == AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT ||
            command->tag == AETHER_PAINT_DRAW_TEXT_GRADIENT) ++out.gradients;
        out.commands.push_back(std::move(command));
    }
    aether_display_list_destroy(list);
    aether_display_list_builder_destroy(builder);
    aether_layout_result_destroy(layout);
    aether_layout_destroy(engine);
    aether_style_map_destroy(map);
}
void rect(const aether_rectf& a, const aether_rectf& b) {
    scalar(a.x,b.x,"rect.x"); scalar(a.y,b.y,"rect.y");
    scalar(a.width,b.width,"rect.width"); scalar(a.height,b.height,"rect.height");
}
void radii(const aether_corner_radii& a, const aether_corner_radii& b) {
    scalar(a.top_left_x,b.top_left_x,"radius.tlx"); scalar(a.top_left_y,b.top_left_y,"radius.tly");
    scalar(a.top_right_x,b.top_right_x,"radius.trx"); scalar(a.top_right_y,b.top_right_y,"radius.try");
    scalar(a.bottom_left_x,b.bottom_left_x,"radius.blx"); scalar(a.bottom_left_y,b.bottom_left_y,"radius.bly");
    scalar(a.bottom_right_x,b.bottom_right_x,"radius.brx"); scalar(a.bottom_right_y,b.bottom_right_y,"radius.bry");
}
void same_text(const char* a, const char* b) {
    require((a != nullptr) == (b != nullptr), "paint text presence");
    if (a && b) require(std::strcmp(a,b)==0,"paint text");
}
void display(const painting& a, const painting& b) {
    require(a.commands.size() == b.commands.size(), "display command count");
    require(a.gradients == b.gradients, "painted gradient count");
    for (size_t i = 0; i < a.commands.size(); ++i) {
        const auto& x=*a.commands[i]; const auto& y=*b.commands[i];
        require(x.tag==y.tag,"paint command tag");
        switch (x.tag) {
        case AETHER_PAINT_FILL_ROUNDED_RECT: {
            const auto& u=x.data.fill_rounded_rect; const auto& v=y.data.fill_rounded_rect;
            rect(u.rect,v.rect); radii(u.radii,v.radii); color(u.color,v.color); break;
        }
        case AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT: {
            const auto& u=x.data.fill_rounded_rect_gradient; const auto& v=y.data.fill_rounded_rect_gradient;
            rect(u.rect,v.rect); rect(u.gradient_rect,v.gradient_rect); radii(u.radii,v.radii);
            gradient(u.gradient,v.gradient); scalar(u.opacity,v.opacity,"gradient opacity"); break;
        }
        case AETHER_PAINT_DRAW_TEXT: {
            const auto& u=x.data.draw_text; const auto& v=y.data.draw_text;
            same_text(u.text,v.text); same_text(u.font_family,v.font_family); rect(u.rect,v.rect);
            scalar(u.font_size,v.font_size,"font size"); color(u.color,v.color);
            require(u.font_weight==v.font_weight && u.text_align==v.text_align && u.wrap==v.wrap,"text flags"); break;
        }
        case AETHER_PAINT_DRAW_TEXT_GRADIENT: {
            const auto& u=x.data.draw_text_gradient; const auto& v=y.data.draw_text_gradient;
            same_text(u.text,v.text); same_text(u.font_family,v.font_family); rect(u.rect,v.rect);
            scalar(u.font_size,v.font_size,"font size"); scalar(u.opacity,v.opacity,"text opacity");
            require(u.font_weight==v.font_weight && u.text_align==v.text_align,"gradient text flags");
            gradient(u.gradient,v.gradient); break;
        }
        case AETHER_PAINT_PUSH_CLIP:
            rect(x.data.push_clip.rect,y.data.push_clip.rect); radii(x.data.push_clip.radii,y.data.push_clip.radii); break;
        case AETHER_PAINT_DRAW_BACKGROUND_IMAGE: {
            const auto& u=x.data.draw_background_image; const auto& v=y.data.draw_background_image;
            same_text(u.source,v.source); rect(u.paint_rect,v.paint_rect);
            length(u.position_x,v.position_x); length(u.position_y,v.position_y);
            length(u.size_x,v.size_x); length(u.size_y,v.size_y);
            scalar(u.opacity,v.opacity,"image opacity"); rect(u.source_rect,v.source_rect); rect(u.destination_rect,v.destination_rect);
            require(u.size_kind==v.size_kind && u.repeat_x==v.repeat_x && u.repeat_y==v.repeat_y &&
                    u.has_resolved_geometry==v.has_resolved_geometry,"image geometry flags"); break;
        }
        case AETHER_PAINT_POP_CLIP:
        case AETHER_PAINT_BEGIN_PAINT_CHUNK:
        case AETHER_PAINT_END_PAINT_CHUNK:
            // Stable IDs belong to different documents; only the structural tags match.
            break;
        default: require(false,"unexpected command in gradient fixture");
        }
    }
}
void select_locale(const std::string& name) {
    require(std::setlocale(LC_NUMERIC,name.c_str()) != nullptr,"select numeric locale");
    require(std::strcmp(std::localeconv()->decimal_point, name == "C" ? "." : ",") == 0,
            "locale must really provide requested decimal separator");
}
struct locale_restore {
    std::string saved = std::setlocale(LC_NUMERIC,nullptr);
    ~locale_restore() { std::setlocale(LC_NUMERIC,saved.c_str()); }
};
struct test_case { std::string name, actual, expected; bool must_paint = false; };
void reference(const std::string& declarations, style& out, painting& painted) {
    select_locale("C");
    fixture f;
    f.set(declarations,false);
    f.copy(out);
    paint(f,painted);
    // Both out and painted are inspected after all producer objects are destroyed.
}
void stable(fixture& f, const style& expected) {
    // This fixed-size fixture needs no settling pass, verified independently
    // against the unchanged baseline with its already-supported angle grammar.
    // Do not hide a spurious second-resolution update behind a retry loop.
    style second;
    require(f.copy(second)==0,"second unchanged resolve must return zero updates");
    snapshot(second.value,expected.value);
    style third;
    require(f.copy(third)==0,"third unchanged resolve must return zero updates");
    snapshot(third.value,expected.value);
}
void run_matrix(bool inlined, const std::vector<test_case>& matrix,
                const std::vector<std::string>& locales) {
    for (const auto& compiled : locales) for (const auto& resolved : locales) {
        fixture f;
        for (const auto& c : matrix) {
            context = std::string(inlined ? "inline/" : "sheet/") + compiled + "->" + resolved + "/" + c.name;
            style expected, actual;
            painting expected_paint, actual_paint;
            reference(c.expected,expected,expected_paint);
            select_locale(compiled);
            // Stylesheets compile here. Inline attributes are staged here and
            // their parser runs at resolution; the public API has no separate
            // inline compilation operation.
            f.set(c.actual,inlined);
            select_locale(resolved);
            f.copy(actual);
            snapshot(actual.value,expected.value);
            stable(f,expected);
            paint(f,actual_paint);
            display(actual_paint,expected_paint);
            if (c.must_paint) require(actual_paint.gradients>0,"accepted finite angle must emit gradient paint");
            if (c.name.find("background-clip") != std::string::npos) {
                require(actual.value.has_text_gradient == expected.value.has_text_gradient,
                        "clip:text gradient presence follows computed validity");
                bool text_command=false;
                for (const auto& command : actual_paint.commands)
                    text_command |= command->tag==AETHER_PAINT_DRAW_TEXT_GRADIENT;
                require(text_command == (expected.value.has_text_gradient != 0),
                        "clip:text command presence follows computed validity");
            }
            snapshot(actual.value,expected.value);
            ++cases;
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    locale_restore restore;
    bool require_all=false;
    for (int i=1;i<argc;++i) {
        if (std::strcmp(argv[i],"--require-all")==0) require_all=true;
        else { std::fprintf(stderr,"usage: %s [--require-all]\n",argv[0]); return 2; }
    }
    std::vector<std::string> locales{"C"};
    const char* families[][4] = {
        {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "German_Germany.1252"},
        {"fr_FR.UTF-8", "fr_FR.utf8", "fr_FR", "French_France.1252"},
        {"tr_TR.UTF-8", "tr_TR.utf8", "tr_TR", "Turkish_Turkey.1254"}
    };
    for (const auto& family : families) {
        bool found=false;
        for (const char* name : family) {
            if (std::setlocale(LC_NUMERIC,name)) {
                select_locale(name); locales.emplace_back(name); found=true; break;
            }
        }
        if (!found) {
            std::fprintf(stderr,"%s: numeric locale family %s unavailable\n",require_all ? "FAIL" : "SKIP",family[0]);
            if (require_all) return 1;
        }
    }
    context="first non-C invocation";
    // First real gradient parse happens in a non-C locale whenever available.
    {
        select_locale(locales.size()>1 ? locales[1] : "C");
        fixture first; style result;
        first.set("background-image:linear-gradient(12.5deg,red,blue)",false);
        first.copy(result);
        require(result.value.background_layers.count==1,"first non-C parse produces gradient");
        const auto& g=result.value.background_layers.items[0].gradient;
        const float radians=12.5f*3.14159265358979323846f/180.f;
        scalar(g.direction_x,std::sin(radians),"first non-C x");
        scalar(g.direction_y,-std::cos(radians),"first non-C y");
    }
    const std::string good="linear-gradient(12.5deg,#126b35 13%,#284fd1 83%)";
    std::vector<test_case> matrix;
    for (const std::string property : {"background-image", "background"}) {
        const std::string initial = property=="background" ? "background:initial;" : lower+"background-image:none;";
        for (const std::string angle : {"0x10deg","0x1p2deg","3.4028236e38deg","-3.4028236e38deg",
                                       "1e100deg","-1e100deg","1.deg","1e+deg","12.5/**/deg","12.5%"}) {
            const std::string bad="linear-gradient("+angle+",#126b35 13%,#284fd1 83%)";
            matrix.push_back({property+"/literal/"+angle,lower+property+":"+bad+";",lower});
            matrix.push_back({property+"/literal-important/"+angle,lower+property+":"+bad+" !important;",lower});
            matrix.push_back({property+"/defined-bad-angle/"+angle,lower+"--angle:"+angle+";"+property+":linear-gradient(var(--angle,180deg),#126b35 13%,#284fd1 83%);",initial});
            matrix.push_back({property+"/defined-bad-gradient/"+angle,lower+"--bad:"+bad+";"+property+":var(--bad,"+fallback+");",initial});
            for (int position=0;position<3;++position) {
                std::string layers;
                for (int layer=0;layer<3;++layer) { if(layer) layers+=","; layers+=layer==position ? bad : good; }
                matrix.push_back({property+"/multilayer-literal/"+angle+"/"+std::to_string(position),lower+property+":"+layers+";",lower});
                matrix.push_back({property+"/multilayer-var/"+angle+"/"+std::to_string(position),lower+"--layers:"+layers+";"+property+":var(--layers);",initial});
            }
        }
        matrix.push_back({property+"/valid-angle-var",lower+"--angle:12.5deg;"+property+":linear-gradient(var(--angle),#126b35 13%,#284fd1 83%);",lower+property+":"+good+";",true});
        matrix.push_back({property+"/valid-full-var",lower+"--g:"+good+";"+property+":var(--g);",lower+property+":"+good+";",true});
        matrix.push_back({property+"/missing-angle-fallback",lower+property+":linear-gradient(var(--missing,12.5deg),#126b35 13%,#284fd1 83%);",lower+property+":"+good+";",true});
        matrix.push_back({property+"/missing-gradient-fallback",lower+property+":var(--missing,"+fallback+");",lower+property+":"+fallback+";",true});
        matrix.push_back({property+"/cannot-create-dimension",lower+"--number:12.5;"+property+":linear-gradient(var(--number)deg,#126b35 13%,#284fd1 83%);",initial});
        matrix.push_back({property+"/valid-important",lower+property+":"+good+"!important;"+property+":"+fallback+";",lower+property+":"+good+";",true});
        for (const auto& angle : std::vector<std::pair<std::string,float>>{{"3e38deg",3e38f},{"-3e38deg",-3e38f}}) {
            // Decimal integer residue uses only an exact float and fmod, never a
            // locale-dependent strtof oracle or formatting of a decimal point.
            const std::string reduced=std::to_string(static_cast<int>(std::fmod(angle.second,360.f)))+"deg";
            const std::string large="linear-gradient("+angle.first+",red,blue)";
            const std::string small="linear-gradient("+reduced+",red,blue)";
            matrix.push_back({property+"/huge/"+angle.first,property+":"+large+";",property+":"+small+";",true});
            matrix.push_back({property+"/huge-var/"+angle.first,"--g:"+large+";"+property+":var(--g);",property+":"+small+";",true});
        }
    }
    // The invalid shorthand resets all five components, but separately winning
    // important longhands survive. Keeping an important image makes the retained
    // position/size/repeat observable even when the shorthand computes invalid.
    const std::string important="background-color:#192b4d!important;background-image:"+lower_image+"!important;"
        "background-position:17% 23px,31px 47%!important;background-size:61px 73%,contain!important;"
        "background-repeat:space round,no-repeat repeat!important;";
    matrix.push_back({"shorthand/important-longhands",important+"--bad:linear-gradient(0x10deg,red,blue);background:var(--bad);",lower,true});
    matrix.push_back({"shorthand/partial-important-longhands","background-image:"+lower_image+"!important;"
        "background-position:17% 23px,31px 47%!important;"+lower+"--bad:linear-gradient(0x1p2deg,red,blue);background:var(--bad);",
        "background:initial;background-image:"+lower_image+";background-position:17% 23px,31px 47%;",true});
    for (const std::string clip : {"background-clip", "-webkit-background-clip"}) {
        matrix.push_back({clip+"/valid", "background-image:"+good+";"+clip+":text;", "background-image:"+good+";"+clip+":text;",true});
        matrix.push_back({clip+"/huge", "background-image:linear-gradient(3e38deg,red,blue);"+clip+":text;",
            "background-image:linear-gradient("+std::to_string(static_cast<int>(std::fmod(3e38f,360.f)))+"deg,red,blue);"+clip+":text;",true});
        matrix.push_back({clip+"/invalid", "background-image:"+good+";background-image:linear-gradient(0x10deg,red,blue);"+clip+":text;",
            "background-image:"+good+";"+clip+":text;",true});
        const std::string clipped_good="background-image:"+good+";"+clip+":text;";
        matrix.push_back({clip+"/valid-angle-var",
            "--angle:12.5deg;background-image:linear-gradient(var(--angle),#126b35 13%,#284fd1 83%);"+clip+":text;",
            clipped_good,true});
        matrix.push_back({clip+"/valid-full-var", "--g:"+good+";background-image:var(--g);"+clip+":text;",
            clipped_good,true});
        for (const std::string angle : {"0x10deg","0x1p2deg","3.4028236e38deg","-1e100deg"}) {
            const std::string bad="linear-gradient("+angle+",red,blue)";
            const std::string cleared="background-image:none;"+clip+":text;";
            matrix.push_back({clip+"/defined-invalid-angle-var/"+angle,
                clipped_good+"--angle:"+angle+";background-image:linear-gradient(var(--angle,180deg),red,blue);",
                cleared});
            // Immediately recover on the same runtime after computed invalidity.
            matrix.push_back({clip+"/recover-angle-var/"+angle,
                "--angle:12.5deg;background-image:linear-gradient(var(--angle),#126b35 13%,#284fd1 83%);"+clip+":text;",
                clipped_good,true});
            matrix.push_back({clip+"/defined-invalid-full-var/"+angle,
                clipped_good+"--bad:"+bad+";background-image:var(--bad,"+fallback+");",
                cleared});
            matrix.push_back({clip+"/recover-full-var/"+angle,
                "--g:"+good+";background-image:var(--g);"+clip+":text;",clipped_good,true});
        }
        for (const auto& angle : std::vector<std::pair<std::string,float>>{{"3e38deg",3e38f},{"-3e38deg",-3e38f}}) {
            const std::string reduced=std::to_string(static_cast<int>(std::fmod(angle.second,360.f)))+"deg";
            const std::string expected="background-image:linear-gradient("+reduced+",red,blue);"+clip+":text;";
            matrix.push_back({clip+"/huge-angle-var/"+angle.first,
                "--angle:"+angle.first+";background-image:linear-gradient(var(--angle),red,blue);"+clip+":text;",
                expected,true});
            matrix.push_back({clip+"/huge-full-var/"+angle.first,
                "--g:linear-gradient("+angle.first+",red,blue);background-image:var(--g);"+clip+":text;",
                expected,true});
        }
    }
    // Explicit adjacent valid -> invalid -> valid mutations in the same runtime.
    matrix.push_back({"mutation/valid", "background-image:"+good+";", "background-image:"+good+";",true});
    matrix.push_back({"mutation/invalid", "background-image:linear-gradient(0x10deg,red,blue);", "background-image:none;"});
    matrix.push_back({"mutation/valid-again", "background-image:"+fallback+";", "background-image:"+fallback+";",true});
    run_matrix(false,matrix,locales);
    run_matrix(true,matrix,locales);
    std::printf("PASS gradient angle runtime: %zu cases, %zu checks, %zu x %zu locales, stylesheet + inline, zero second-resolution updates (NDEBUG-safe)\n",
                cases,checks,locales.size(),locales.size());
}
