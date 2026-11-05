#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

struct fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    ~fixture() {
        if (runtime) aether_style_runtime_destroy(runtime);
        if (doc) aether_doc_destroy(doc);
    }
    float width(const std::string& css, float vw, float vh) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc,
            "<!doctype html><html><body><div id='target'></div></body></html>") == AETHER_OK, "parse document");
        aether_style_runtime_options_t options{};
        const aether_style_environment_t env{vw, vh, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK, "create runtime");
        int64_t sheet = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 2, "media-percentage-lengths", &sheet, nullptr) == AETHER_OK, "add stylesheet");
        aether_style_update_t* updates = nullptr;
        const int count = aether_style_runtime_resolve_subtree(runtime, reinterpret_cast<aether_node_t>(doc), &updates);
        float result = 0;
        bool found = false;
        try {
            require(count >= 0, "resolve subtree");
            // Query after Resolve; only fresh handles expose the computed style.
            const auto target = aether_doc_get_element_by_id(doc, "target");
            require(target != nullptr, "fresh target");
            for (int i = 0; i < count; ++i) {
                aether_node_t node = nullptr;
                aether_computed_style_handle_t handle = nullptr;
                const auto status = aether_style_runtime_get_update(updates[i], &node, &handle, nullptr);
                if (status != AETHER_OK || !handle) {
                    if (handle) aether_computed_style_handle_destroy(handle);
                    require(false, "get update");
                }
                aether_computed_style_t style{};
                const auto copied = aether_computed_style_copy(handle, &style);
                aether_computed_style_handle_destroy(handle);
                require(copied == AETHER_OK, "copy computed style");
                if (node == target) {
                    result = aether_css_length_resolve(style.width, vw, 16, 16, vw, vh);
                    found = true;
                }
                aether_computed_style_free(&style);
            }
        } catch (...) { aether_style_updates_free(updates, count); throw; }
        aether_style_updates_free(updates, count);
        require(found && std::isfinite(result), "target computed length accepted");
        return result;
    }
};

} // namespace
#include "media_v3_cases.h"

// Public ABI only: link this executable against either the baseline or final
// libaether_core. Diagnostic propagation is tested at the AST entrypoint.
int main() {
    int total=0, failures=0;
    for (float vw:{320.f,800.f}) for (const auto& c:media_v3::cases(vw,600)) {
        ++total;
        try {
            fixture f;
            const float actual=f.width("#target{width:11px}@media "+c.query+"{#target{width:37px}}",vw,600);
            const float expected=c.matches?37.f:11.f;
            if (std::fabs(actual-expected)>.001f) {
                ++failures;
                std::fprintf(stderr,"FAIL viewport=%.0f query=%s expected=%.0f actual=%.3f\n",vw,c.query.c_str(),expected,actual);
            }
        } catch (const std::exception& e) {
            ++failures; std::fprintf(stderr,"FAIL %s: %s\n",c.query.c_str(),e.what());
        }
    }
    // Nested stylesheet extraction must use CSS string-escape semantics too:
    // a hex escape consumes its newline terminator without ending the string.
    const char* escaped_strings[]={"\"\\61\n\"", "\"\\000061\n\"",
        "url(\"\\61\n\")", "\"\\61\r\n\"", "\"\\61\f\""};
    for (float vw:{320.f,800.f}) for (auto value:escaped_strings) for (bool inner:{false,true}) {
        ++total;
        const std::string q="not print and (min-width:"+std::string(value)+")";
        const std::string css="#target{width:11px}@media "+(inner?std::string("screen"):q)+
            "{@media "+(inner?q:std::string("screen"))+"{#target{width:37px}}}";
        try {
            fixture f;
            const float actual=f.width(css,vw,600);
            if (std::fabs(actual-37.f)>.001f) {
                ++failures;
                std::fprintf(stderr,"FAIL nested hex-string viewport=%.0f inner=%d value=%s expected=37 actual=%.3f\n",
                    vw,inner,value,actual);
            }
        } catch (const std::exception& e) {
            ++failures; std::fprintf(stderr,"FAIL nested hex-string: %s\n",e.what());
        }
    }
    // A valid custom-property string must not swallow the surrounding ordinary
    // declaration block, even though these tests do not interpret its value.
    const char* ordinary_strings[]={"\"a\\\nb\"", "\"a\\\r\nb\"",
        "\"a\\61\nb\"", "\"a\\61\r\nb\"", "\"a\\61\fb\""};
    for (auto value:ordinary_strings) {
        ++total;
        try {
            fixture f;
            const float actual=f.width("#target{width:11px}#target{width:37px;--x:"+std::string(value)+"}",800,600);
            if (std::fabs(actual-37.f)>.001f) {
                ++failures;
                std::fprintf(stderr,"FAIL ordinary string value=%s expected=37 actual=%.3f\n",value,actual);
            }
        } catch (const std::exception& e) {
            ++failures; std::fprintf(stderr,"FAIL ordinary string: %s\n",e.what());
        }
    }
    std::printf("MEDIA V3 ABI observations=%d failures=%d\n",total,failures);
    return failures?1:0;
}
