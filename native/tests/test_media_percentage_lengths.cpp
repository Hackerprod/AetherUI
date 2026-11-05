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
    float width(const std::string& css) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc,
            "<!doctype html><html><body><div id='target'></div></body></html>") == AETHER_OK, "parse document");
        aether_style_runtime_options_t options{};
        const aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
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
                    result = aether_css_length_resolve(style.width, 800, 16, 16, 800, 600);
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

struct media_case { const char *id, *query; float expected; };
const media_case media_cases[] = {
    {"B01", "(min-width:0%)", 11},
    {"B02", "(min-width:calc(50% - 50% + 100px))", 11},
    {"B03", "(min-height:calc(0 * 50% + 100px))", 11},
    {"B04", "not screen and (min-width:50%)", 11},
    {"B05", "not screen and (min-width:50%) and (max-width:100px)", 37},
    {"B06", "(min-width:50%), (min-width:100px)", 37},
    {"B07", "(min-width:50%), (max-width:100px)", 11},
    {"B08", "not screen and (max-width:100px)", 37},
    {"B09", "not screen and (min-width:100px)", 11},
    {"B10", "(min-width:100px/* % */)", 37},
    {"B11", nullptr, 11},
    {"B12", nullptr, 11},
};
struct ordinary_case { const char *id, *value; float expected; };
const ordinary_case ordinary_cases[] = {
    {"C01", "50%", 400},
    {"C02", "calc(50% + 10px)", 410},
    {"C03", "calc(50% - 50% + 100px)", 100},
    {"C04", "calc(0 * 50% + 100px)", 100},
};
int failures = 0;
void observe(const char* id, const std::string& css, float expected) {
    try {
        fixture f;
        const float actual = f.width(css);
        const bool pass = std::fabs(actual - expected) < .001f;
        std::printf("MEDIA %s expected=%.3f actual=%.3f pass=%d\n", id, expected, actual, pass ? 1 : 0);
        if (!pass) ++failures;
    } catch (const std::exception& error) {
        ++failures;
        std::fprintf(stderr, "MEDIA %s controlled failure: %s\n", id, error.what());
    }
}
} // namespace

int main() {
    static_assert(sizeof(media_cases) / sizeof(media_cases[0]) == 12);
    static_assert(sizeof(ordinary_cases) / sizeof(ordinary_cases[0]) == 4);
    for (int i = 0; i < 12; ++i) {
        const auto& c = media_cases[i];
        std::string conditional;
        if (c.query) conditional = "@media " + std::string(c.query) + "{#target{width:37px}}";
        else if (i == 10) conditional = "@media (min-width:100px){@media (min-width:50%){#target{width:37px}}}";
        else conditional = "@media (min-width:50%){@media (min-width:100px){#target{width:37px}}}";
        observe(c.id, "#target{width:11px}" + conditional, c.expected);
    }
    const int media_failures = failures;
    for (const auto& c : ordinary_cases)
        observe(c.id, "#target{width:" + std::string(c.value) + "}", c.expected);
    std::printf("MEDIA B observations=12 failures=%d C observations=4 failures=%d TOTAL observations=16 failures=%d\n",
                media_failures, failures - media_failures, failures);
    return failures ? 1 : 0;
}
