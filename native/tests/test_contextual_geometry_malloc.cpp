// Link the actual core objects with --wrap=malloc: an executable linked only
// to the shared library cannot intercept that library's internal malloc calls.
#define main geometry_reference_suite
#include "test_contextual_used_geometry.cpp"
#undef main
#include <cstring>
static int failure_index = -1;
static bool failed_allocation = false;
extern "C" void* __real_malloc(size_t);
extern "C" void* __wrap_malloc(size_t size) {
    if (failure_index >= 0 && failure_index-- == 0) {
        failed_allocation = true; failure_index = -1; return nullptr;
    }
    return __real_malloc(size);
}
template<class Operation> static int inject(Operation operation) {
    int failures = 0;
    for (int index=0; index<1000; ++index) {
        failed_allocation=false; failure_index=index;
        const auto status=operation();
        failure_index=-1;
        if (!failed_allocation) { REQUIRE(status==AETHER_OK); return failures; }
        REQUIRE(status==AETHER_ERROR_OUT_OF_MEMORY); ++failures;
    }
    REQUIRE(false); return failures;
}
int main() {
    fixture f("#p{display:flex;text-shadow:1px 2px red;background-image:linear-gradient(red,blue)}", "anonymous text");
    aether_layout_set_contextual_text_measurer(f.engine,
        [](const char*, const aether_computed_style_t*, float height, float, void*) -> aether_text_metrics_t { return {40,height,height*.8f}; }, nullptr);
    aether_layout_result_t measured{};
    REQUIRE(aether_layout_layout(f.engine,f.doc,f.map,800,600,nullptr,nullptr,&measured)==AETHER_OK);
    aether_layout_result_destroy(f.layout); f.layout=measured;
    int failures=inject([&] {
        auto output=f.layout;
        const auto status=aether_layout_layout(f.engine,f.doc,f.map,800,600,nullptr,nullptr,&output);
        if(status==AETHER_OK) aether_layout_result_destroy(output);
        else REQUIRE(output==f.layout);
        return status;
    });
    REQUIRE(failures>0);
    aether_display_list_builder_t builder{};
    REQUIRE(aether_display_list_builder_create(nullptr,nullptr,&builder)==AETHER_OK);
    aether_display_list_t baseline{};
    REQUIRE(aether_display_list_build(builder,f.layout,nullptr,&baseline)==AETHER_OK);
    failures+=inject([&] {
        auto output=baseline;
        const auto status=aether_display_list_build(builder,f.layout,nullptr,&output);
        if(status==AETHER_OK) {
            REQUIRE(aether_display_list_get_count(output)==aether_display_list_get_count(baseline));
            aether_display_list_destroy(output);
        } else REQUIRE(output==baseline);
        return status;
    });
    for(int index=0; index<aether_display_list_get_count(baseline); ++index) {
        failures+=inject([&] {
            aether_paint_command_t output{}; output.tag=AETHER_PAINT_POP_CLIP;
            const auto original=output;
            const auto status=aether_display_list_get_command(baseline,index,&output);
            if(status==AETHER_OK) aether_paint_command_free(&output);
            else REQUIRE(std::memcmp(&output,&original,sizeof(output))==0);
            return status;
        });
    }
    aether_display_list_destroy(baseline); aether_display_list_builder_destroy(builder);
    std::printf("contextual geometry malloc: %d failure boundaries passed\n",failures);
}
