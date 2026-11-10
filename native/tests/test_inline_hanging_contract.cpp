#define main hanging_reference_suite
#include "test_inline_hanging.cpp"
#undef main
#include <limits>

struct CallbackCase {
    int kind = 0, calls = 0, trigger = 3;
    aether_node_t parent = nullptr, child = nullptr;
    aether_status_t mutation_status = AETHER_OK;
    static aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void* data) {
        auto& c = *static_cast<CallbackCase*>(data); ++c.calls;
        aether_text_metrics_t metrics{float(std::strlen(text))*10,20,15};
        // Reject after the prefix has already been measured, including while
        // collecting the pending unit rather than only the first callback.
        if (c.calls == c.trigger) {
            if (c.kind == 0) metrics.width = std::numeric_limits<float>::quiet_NaN();
            if (c.kind == 1) metrics.height = -1;
            if (c.kind == 2) metrics.baseline = std::numeric_limits<float>::infinity();
            if (c.kind == 3) c.mutation_status = aether_node_remove_child(c.parent, c.child);
        }
        return metrics;
    }
    static aether_text_metrics_t contextual(const char* text, const aether_computed_style_t* style,
                                            float, float width, void* data) {
        return measure(text, style, width, data);
    }
};
int main() {
    for (int trigger : {3, 5}) for (bool contextual : {false, true}) for (int kind = 0; kind < 4; ++kind) {
        scenario = "callback transaction " + std::to_string(kind) + (contextual ? "/contextual" : "/legacy");
        Fixture f(trigger == 3 ? "aa    bb" : "aa    bb    cc");
        auto retained = f.result; auto box = f.box(); const auto rect = f.rect();
        auto fragments = f.runs();
        CallbackCase callback; callback.kind = kind; callback.trigger = trigger;
        callback.parent = aether_doc_get_element_by_id(f.doc, "host");
        callback.child = aether_node_get_child(callback.parent, 0);
        aether_layout_engine_t engine{};
        need(aether_layout_create(CallbackCase::measure, &callback, &engine) == AETHER_OK, "fault engine");
        if (contextual) aether_layout_set_contextual_text_measurer(engine, CallbackCase::contextual, &callback);
        auto output = retained;
        auto status = aether_layout_layout(engine, f.doc, f.map, 600, 400, nullptr, nullptr, &output);
        check(status != AETHER_OK, "invalid callback aborts");
        check(output == retained, "failure leaves output untouched");
        check(callback.calls == trigger, "abort immediately before consuming invalid decision");
        if (kind == 3) check(callback.mutation_status == AETHER_OK, "mutation happened");
        near(aether_layout_box_get_border_box(box).height, rect.height, "retained box remains valid");
        std::vector<Run> after; Fixture::collect(box, after);
        check(after.size() == fragments.size(), "retained fragment count");
        for (size_t i=0; i<after.size() && i<fragments.size(); ++i) {
            check(after[i].text == fragments[i].text, "retained fragment text");
            near(after[i].rect.x, fragments[i].rect.x, "retained fragment x");
            near(after[i].rect.y, fragments[i].rect.y, "retained fragment y");
        }
        aether_layout_destroy(engine);
    }
    { scenario="source-order metrics reused"; Fixture f("aa    bb");
      check(f.meter.calls == std::vector<std::string>{"aa", "    ", "bb"}, "one ordered measurement per token in block pass"); }
    for (const char* css : {"padding-left:1px", "border-left:1px solid red"}) {
        scenario="decorated empty inline fallback";
        Fixture f(std::string("aa    <span style='")+css+"'></span>bb");
        near(f.rect().height,60,"complete legacy sequence fallback");
    }
    for (const char* body : {"aa    <span style='white-space:normal'> </span>bb",
                           "aa    <span style='white-space:nowrap'> bb</span>",
                           "aa    <span style='white-space:nowrap'>bb </span>"}) {
        scenario="ambiguous collapse fallback"; Fixture f(body);
        // The old physical fragmentation is retained, not disguised by H.
        auto runs=f.runs(); bool old_space_line=false;
        for (const auto& run:runs) if(run.text=="    ") old_space_line=run.rect.y-f.rect().y==20;
        check(old_space_line,"fallback before any new line decisions");
    }
    for(const char* body : {"aa    <span style='white-space:pre-line'> </span>bb",
                           "aa\t\t\t\tbb", "aa    b\xc3\xa9"}) {
      scenario="out of scope sequence fallback"; Fixture f(body);auto runs=f.runs();
      check(runs.size()>=2 && runs[1].rect.y-f.rect().y==20,"unsupported sequence uses complete legacy geometry");
    }
    {scenario="nested decoration fallback"; Fixture f("<span style='padding-right:1px'><span>aa    bb</span></span>");near(f.rect().height,60,"ancestor barrier survives flattening");}
    {scenario="nowrap interior split across runs"; Fixture f("aa  <span style='white-space:nowrap'>b</span><span style='white-space:nowrap'> </span><span style='white-space:nowrap'>cc</span>");
     near(f.rect().height,40,"single indivisible nowrap unit");f.expect({{"aa",{0,0,20,20}},{"  ",{20,0,20,20}},{"b",{0,20,10,20}},{" ",{10,20,10,20}},{"cc",{20,20,20,20}}});}
    {scenario="PRE suffix fence";Fixture f("aa  <span style='white-space:pre'>bb    </span>","#host{text-align:center}");
     near(f.rect().height,40,"PRE unit moved intact");f.expect({{"aa",{15,0,20,20}},{"  ",{35,0,20,20}},{"bb",{0,20,20,20}},{"    ",{20,20,40,20}}});}
    {scenario="one-pixel excess"; Fixture f("aa    bb", "#host{width:59px}");near(f.rect().height,40,"one-pixel tail still hangs");}
    {scenario="exact edge"; Fixture f("aa    bb", "#host{width:60px}");near(f.rect().height,40,"full following unit cannot fit");}
    {scenario="very long tail";Fixture f("aa"+std::string(200,' ')+"bb");near(f.rect().height,40,"no repeated whitespace wraps");}
    {scenario="spaces only";Fixture f(std::string(200,' '));near(f.rect().height,20,"one preserved line");}
    {scenario="atomic after hanging";Fixture f("aa    <span id='atom' style='display:inline-block;width:20px;height:20px'></span>");
     auto b=aether_layout_result_find_primary_box(f.result,aether_doc_get_element_by_id(f.doc,"atom"));need(b,"atom");
     near(aether_layout_box_get_border_box(b).y-f.rect().y,20,"atom moved once to second line");near(f.rect().height,40,"atomic line height");}
    {scenario="atomic public paint order";Fixture f("aa    <span id='atom' style='display:inline-block;width:20px'>z</span>cc");
     std::vector<std::string> texts;
     for(int i=0;i<aether_layout_result_get_paint_order_count(f.result);++i) {
       auto b=aether_layout_result_get_paint_order_box(f.result,i);char* text=aether_layout_box_get_text(b);
       if(text&&*text)texts.emplace_back(text);aether_string_free(text);
     }
     check(texts==std::vector<std::string>{"aa","    ","z","cc"},"public index retains DOM paint order");}
    {scenario="justify excludes tail";Fixture f("a a    bb","#host{width:70px;text-align:justify}");
     f.expect({{"a",{0,0,10,20}},{" ",{10,0,50,20}},{"a",{60,0,10,20}},{"    ",{70,0,40,20}},{"bb",{0,20,20,20}}});}
    std::printf("inline-hanging-contract: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
