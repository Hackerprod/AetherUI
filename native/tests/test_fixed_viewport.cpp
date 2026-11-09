#include "animations/transition_runtime.h"
#include "layout/layout_types.h"
#include "immutable_style_test_support.h"
#include <aether/aether.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL %s\n", what);
    std::exit(1);
  }
}
static bool near(float a, float b) { return std::fabs(a - b) < .01f; }
static aether_text_metrics_t
measure(const char *s, const aether_computed_style_t *, float, void *) {
  return {float(std::strlen(s)) * 10, 16, 12};
}
struct fixture {
  aether_doc_t doc{};
  aether_style_runtime_t styles_runtime{};
  aether_style_map_t styles{};
  aether_layout_engine_t engine{};
  aether_layout_result_t result{};
  aether_transition_runtime_t transitions{};
  std::vector<aether_computed_style_handle_t> independent_styles;
  fixture(const std::string &css = "", const std::string &item = "",
          const std::string &children = "", bool prefix = false) {
    const std::string markup =
        "<html id='html'><body id='body'><div id='outer'><div id='item'><div "
        "id='toggler'>" +
        (children.empty() ? "<div id='fixed'><div id='leaf'></div></div>"
                          : children) +
        "</div></div></div><div id='other'></div></body></html>";
    auto final_markup = markup;
    if (prefix) {
      const auto item_pos = final_markup.find("<div id='item'>");
      final_markup.insert(item_pos, "<span id='prefix'></span>");
    }
    check(aether_doc_create(&doc) == AETHER_OK &&
              aether_doc_parse_html(doc, final_markup.c_str()) == AETHER_OK,
          "parse");
    aether_style_runtime_options_t opts{};
    aether_style_environment_t env{360, 340, 1, 16, 0, 0, 0};
    check(aether_style_runtime_create(&opts, &env, nullptr, nullptr,
                                      &styles_runtime) == AETHER_OK,
          "style runtime");
    const auto rules =
        std::string("html,body,div{display:block;margin:0}body{width:360px;"
                    "height:340px}#outer{width:360px;height:340px;display:flex;"
                    "justify-content:center;align-items:center}#item{width:"
                    "100px;height:80px;flex-shrink:0;") +
        item +
        "}#fixed{position:fixed;left:20px;top:20px;width:30px;height:30px}#"
        "leaf{margin-left:10px;margin-top:10px;width:10px;height:10px}" +
        css;
    int64_t id{};
    int32_t count{};
    check(aether_style_runtime_add_stylesheet(styles_runtime, rules.c_str(), 0,
                                              "fixed.css", &id,
                                              &count) == AETHER_OK,
          "CSS");
    resolve();
    check(aether_layout_create(measure, nullptr, &engine) == AETHER_OK,
          "engine");
    check(aether_transition_runtime_create(&transitions) == AETHER_OK,
          "transitions");
  }
  ~fixture() {
    for (auto* style : independent_styles) aether_computed_style_handle_destroy(style);
    aether_layout_result_destroy(result);
    aether_layout_destroy(engine);
    aether_transition_runtime_destroy(transitions);
    aether_style_map_destroy(styles);
    aether_style_runtime_destroy(styles_runtime);
    aether_doc_destroy(doc);
  }
  aether_node_t node(const char *id) {
    return aether_doc_get_element_by_id(doc, id);
  }
  uint32_t resolve() {
    aether_style_update_t *updates{};
    int n = aether_style_runtime_resolve_subtree(
        styles_runtime, aether_doc_get_document_element(doc), &updates);
    check(n >= 0, "resolve");
    uint32_t impact = 0;
    for (int i = 0; i < n; i++) {
      uint32_t bits{};
      check(aether_style_runtime_get_update(updates[i], nullptr, nullptr,
                                            &bits) == AETHER_OK,
            "update impact");
      impact |= bits;
    }
    aether_style_updates_free(updates, n);
    aether_style_map_destroy(styles);
    styles = nullptr;
    check(aether_style_runtime_create_resolved_style_map(styles_runtime, doc,
                                                         &styles) == AETHER_OK,
          "map");
    return impact;
  }
  bool prepare(aether_transition_runtime_t runtime = nullptr) {
    aether_bool_t changed = -1;
    check(aether_layout_prepare_transition_context(
              engine, runtime, styles, result, &changed) == AETHER_OK,
          "prepare");
    return changed != 0;
  }
  void layout(bool prepared = true, float w = 360, float h = 340) {
    if (prepared)
      prepare(transitions);
    aether_layout_result_t next{};
    check(aether_layout_layout(engine, doc, styles, w, h, nullptr, nullptr,
                               &next) == AETHER_OK,
          "layout");
    aether_layout_result_destroy(result);
    result = next;
  }
  std::array<aether_rectf, 4> rects(const char *id) {
    auto b = aether_layout_result_find_primary_box(result, node(id));
    check(b != nullptr, "box");
    return {aether_layout_box_get_margin_box(b),
            aether_layout_box_get_border_box(b),
            aether_layout_box_get_padding_box(b),
            aether_layout_box_get_content_box(b)};
  }
  void xy(const char *id, float x, float y) {
    int i = 0;
    for (auto r : rects(id)) {
      const float margin = (i++ == 0 && (std::strcmp(id, "leaf") == 0 ||
                                         std::strcmp(id, "nested-leaf") == 0))
                               ? 10.f
                               : 0.f;
      if (!near(r.x, x - margin) || !near(r.y, y - margin)) {
        std::fprintf(stderr, "%s actual(%g,%g),expected(%g,%g)\n", id, r.x, r.y,
                     x, y);
        check(false, "four rect origins");
      }
    }
  }
  aether_computed_style_t style(const char *id) {
    auto* copy = aether_style_map_get(styles, node(id));
    check(copy != nullptr, "style");
    try { independent_styles.push_back(copy); }
    catch (...) { aether_computed_style_handle_destroy(copy); throw; }
    // This descriptor borrows an independent PUBLIC copy owned by this fixture,
    // never frozen backing; local scalar/descriptor edits cannot alter the map.
    return copy->style;
  }
};

static void migration_and_errors() {
  fixture f;
  f.layout(false);
  f.xy("fixed", 150, 150);
  check(!f.result->effective_context_enabled, "legacy disabled");
  check(f.prepare(), "empty E migration changes");
  f.layout();
  f.xy("fixed", 20, 20);
  f.xy("leaf", 30, 30);
  check(!f.prepare(), "prepared stable");
  aether_bool_t changed = 99;
  check(aether_layout_prepare_transition_context(nullptr, nullptr, f.styles,
                                                 f.result, &changed) ==
                AETHER_ERROR_NULL_ARG &&
            changed == 0,
        "null engine status");
  changed = 99;
  check(aether_layout_prepare_transition_context(f.engine, nullptr, nullptr,
                                                 f.result, &changed) ==
                AETHER_ERROR_NULL_ARG &&
            changed == 0,
        "null map status");
  check(aether_layout_prepare_transition_context(f.engine, nullptr, f.styles,
                                                 f.result, nullptr) ==
            AETHER_ERROR_NULL_ARG,
        "null output status");
  f.prepare();
  aether_layout_result_t unused{};
  check(aether_layout_layout(f.engine, f.doc, f.styles, 0, 340, nullptr,
                             nullptr, &unused) == AETHER_ERROR_INVALID_STATE,
        "failed layout");
  f.layout(false);
  f.xy("fixed", 150, 150);
  check(!f.result->effective_context_enabled, "failure consumed scratch");
  f.prepare();
  f.prepare();
  f.layout(false);
  f.xy("fixed", 20, 20);
  f.layout(false);
  f.xy("fixed", 150, 150);
  // Destroy runtime before consuming prepare; only value handles are retained.
  f.prepare(f.transitions);
  aether_transition_runtime_destroy(f.transitions);
  f.transitions = nullptr;
  f.layout(false);
  f.xy("fixed", 20, 20);
  std::puts(
      "PASS H migration, null statuses, one-shot/failure/replacement/lifetime");
}
static void static_matrix() {
  struct row {
    const char *css;
    const char *item;
    float x, y;
  };
  const row rows[] = {
      {"#outer{display:block}", "", 20, 20},
      {"#outer{justify-content:flex-start;align-items:flex-start}", "", 20, 20},
      {"#outer{align-items:flex-start}", "", 20, 20},
      {"#outer{justify-content:flex-end;align-items:flex-start}", "", 20, 20},
      {"#outer{justify-content:flex-start}", "", 20, 20},
      {"", "", 20, 20},
      {"#outer{flex-direction:column}", "", 20, 20},
      {"#outer{width:100px;flex-direction:column;justify-content:flex-start}#"
       "fixed{left:80px;top:80px}",
       "width:160px;", 80, 80},
      {"#outer{display:block}", "position:relative;left:60px;top:60px;", 20,
       20},
      {"#toggler{display:block}", "", 20, 20},
      {"#fixed{position:absolute}", "position:relative;", 150, 150}};
  for (auto &r : rows) {
    fixture f(r.css, r.item);
    f.layout();
    f.xy("fixed", r.x, r.y);
    f.xy("leaf", r.x + 10, r.y + 10);
  }
  std::puts("PASS core 11 explicit public geometry cases");
  // A: Static positions are independent per axis; direct flex handled by
  // existing suite/harness.
  for (bool center : {false, true})
    for (int axes = 0; axes < 3; axes++) {
      std::string css =
          center ? ""
                 : "#outer{justify-content:flex-start;align-items:flex-start}";
      if (axes != 1)
        css += "#fixed{left:auto;right:auto}";
      if (axes != 0)
        css += "#fixed{top:auto;bottom:auto}";
      fixture f(css);
      f.layout();
      const float flow = center ? 130 : 0;
      f.xy("fixed", axes == 1 ? 20 : flow, axes == 0 ? 20 : flow);
    }
  // B: requested fixed roots move, descendant fixed barriers and TEXT remain
  // distinct.
  for (auto external : {"", "#outer{width:100px;flex-direction:column;justify-"
                            "content:flex-start}#item{width:160px}"})
    for (auto display : {"block", "flex"}) {
      fixture f(std::string(external) + "#fixed{display:" + display +
                    "}#nested{position:fixed;left:80px;top:80px;width:30px;"
                    "height:30px}#nested-leaf{margin-left:10px;margin-top:10px;"
                    "width:10px;height:10px}#normal{width:10px;height:10px;"
                    "margin-left:10px;margin-top:10px}#absolute{position:"
                    "absolute;left:10px;top:10px;width:10px;height:10px}",
                "",
                "<div id='fixed'>T<div id='normal'></div><div "
                "id='absolute'></div><div id='nested'><div "
                "id='nested-leaf'></div></div></div>");
      f.layout();
      f.xy("fixed", 20, 20);
      f.xy("nested", 80, 80);
      f.xy("nested-leaf", 90, 90);
      f.xy("absolute", 30, 30);
      auto *box = f.result->find_primary_box(f.node("fixed"));
      for (auto *child : box->children)
        if (child->kind == LAYOUT_BOX_TEXT)
          check(near(child->border_box.x, 20), "text follows fixed root");
    }
  // D: auto margin / atomic inline use the same logical placement contract.
  {
    fixture f("#outer{align-items:flex-start;justify-content:flex-start}#fixed{"
              "top:20px}",
              "margin-left:auto;");
    f.layout();
    f.xy("fixed", 20, 20);
  }
  {
    fixture f("#outer{display:block;text-align:center}#item{display:inline-"
              "block}#toggler{position:relative}");
    f.layout();
    f.xy("fixed", 20, 20);
  }
  // E: viewport-resolved insets remain stable under repeated resize.
  for (bool percent : {false, true}) {
    fixture f(percent ? "#fixed{left:10%;top:10%}"
                      : "#fixed{left:auto;top:auto;right:20px;bottom:20px}");
    for (auto size : {aether_sizef{360, 340}, aether_sizef{400, 380},
                      aether_sizef{360, 340}}) {
      f.layout(true, size.width, size.height);
      f.xy("fixed", percent ? size.width * .1f : size.width - 50,
           percent ? size.height * .1f : size.height - 50);
    }
  }
  // G: only context inside the moved subtree exempts fixed descendants.
  for (auto context :
       {"", "transform:translateX(10px);", "transform:translate(0);",
        "backdrop-filter:blur(0);", "backdrop-filter:blur();",
        "backdrop-filter:blur(calc(0px - 1em));", "opacity:.99;"}) {
    fixture f("", context);
    f.layout();
    const bool local =
        std::strstr(context, "transform") || std::strstr(context, "backdrop");
    f.xy("fixed", local ? 150 : 20, local ? 150 : 20);
  }
  {
    fixture f("#outer{transform:translateX(10px)}");
    f.layout();
    f.xy("fixed", 20, 20);
  }
  std::puts("PASS A/B/D/E/G static guards (raw; visual matrices verified "
            "separately)");
}

static aether_transform_operation translate(float x) {
  aether_transform_operation op{};
  op.kind = AETHER_TRANSFORM_TRANSLATE;
  op.x = {x, AETHER_CSS_UNIT_PX};
  op.y = {0, AETHER_CSS_UNIT_PX};
  return op;
}
static void temporal() {
  fixture f;
  auto node = f.node("toggler");
  auto op = translate(10);
  auto before = f.style("toggler"), after = f.style("toggler");
  before.transforms = {&op, 1};
  after.transition_properties = AETHER_TRANSITION_PROPERTY_TRANSFORM;
  after.transition_duration = .1f;
  after.transition_timing = AETHER_TRANSITION_TIMING_LINEAR;
  check(aether_transition_runtime_observe(f.transitions, node, &before, &after,
                                          1000) == AETHER_OK,
        "observe exit");
  f.layout();
  f.xy("fixed", 150, 150);
  check(!f.prepare(f.transitions), "active stable");
  aether_transition_runtime_tick(f.transitions, 1050);
  f.layout();
  f.xy("fixed", 150, 150);
  aether_transform_matrix matrix{};
  check(aether_transition_runtime_transform_override(f.transitions, node,
                                                     &matrix) &&
            near(matrix.dx, 5),
        "mid matrix");
  // An unrelated style recompute must not restart this destination.
  check(aether_transition_runtime_observe(f.transitions, node, &after, &after,
                                          1060) == AETHER_OK,
        "unchanged observe");
  aether_transition_runtime_tick(f.transitions, 1100);
  check(f.prepare(f.transitions), "individual completion boundary");
  f.layout();
  f.xy("fixed", 20, 20);
  check(aether_transition_runtime_transform_override(f.transitions, node,
                                                     &matrix) &&
            near(matrix.dx, 0),
        "finished cache stays visual only");
  check(!f.prepare(f.transitions), "finished stable");
  // Interruption samples current time even without a preceding tick.
  aether_transition_runtime_observe(f.transitions, node, &before, &after, 2000);
  auto op20 = translate(20);
  auto target = after;
  target.transforms = {&op20, 1};
  aether_transition_runtime_observe(f.transitions, node, &after, &target, 2050);
  check(aether_transition_runtime_transform_override(f.transitions, node,
                                                     &matrix) &&
            near(matrix.dx, 5),
        "interruption samples at now");
  aether_transition_runtime_tick(f.transitions, 2100);
  check(aether_transition_runtime_transform_override(f.transitions, node,
                                                     &matrix) &&
            near(matrix.dx, 12.5f),
        "interrupted interpolation");
  auto cancelled = after;
  cancelled.transition_duration = 0;
  aether_transition_runtime_observe(f.transitions, node, &target, &cancelled,
                                    2100);
  check(!aether_transition_runtime_transform_override(f.transitions, node,
                                                      &matrix),
        "duration cancellation removes active and finished");
  f.layout();
  f.xy("fixed", 20, 20);
  after.transition_delay = .1f;
  aether_transition_runtime_observe(f.transitions, node, &before, &after, 3000);
  f.layout();
  f.xy("fixed", 150, 150);
  check(aether_transition_runtime_transform_override(f.transitions, node,
                                                     &matrix) &&
            near(matrix.dx, 10),
        "positive delay initial");
  cancelled.transition_properties = 0;
  aether_transition_runtime_observe(f.transitions, node, &after, &cancelled,
                                    3000);
  after.transition_delay = -.2f;
  aether_transition_runtime_observe(f.transitions, node, &before, &after, 4000);
  check(!aether_transition_runtime_has_active(f.transitions),
        "negative delay completes on observe");
  f.layout();
  f.xy("fixed", 20, 20);
  // One transform completes while another transform and opacity remain active.
  after.transition_delay = 0;
  after.transition_duration = .1f;
  aether_transition_runtime_observe(f.transitions, node, &before, &after, 5000);
  auto slow = after;
  slow.transition_duration = 1;
  slow.transition_properties |= AETHER_TRANSITION_PROPERTY_OPACITY;
  slow.opacity = .5f;
  aether_transition_runtime_observe(f.transitions, f.node("other"), &before,
                                    &slow, 5000);
  f.layout();
  aether_transition_runtime_tick(f.transitions, 5100);
  check(aether_transition_runtime_has_active(f.transitions),
        "other transitions still active");
  check(f.prepare(f.transitions),
        "global activity cannot mask first completion");
  f.layout();
  f.xy("fixed", 20, 20);
  // Hidden/html contexts belong to the complete map domain, not primary boxes.
  check(!f.prepare(f.transitions), "full-map stable");

  std::puts("PASS H active/finished, interruption, cancellation, delays, "
            "individual completion");
}
static void remaining_guards() {
  // C: independent box-model oracle for a decorated relative flex item.
  fixture c("#normal{width:10px;height:10px}#absolute{position:absolute;left:"
            "10px;top:10px;width:10px;height:10px}",
            "position:relative;left:15px;top:-10px;margin:3px;padding:4px;"
            "border:2px solid black;",
            "<div id='normal'></div><div id='absolute'></div><div "
            "id='fixed'><div id='leaf'></div></div>");
  c.layout();
  c.xy("fixed", 20, 20);
  c.xy("leaf", 30, 30);
  c.xy("normal", 145, 120); // Baseline public-pipeline measurement and Chrome
                            // comparison are recorded
  // in controls(-old)/. Nested static-wrapper absolute padding is preexisting:
  // baseline=candidate (155,130), Chrome (151,126). Preserve that behavior.
  c.xy("absolute", 155, 130);
  const std::array<aether_rectf, 4> expected{{{136, 111, 118, 98},
                                              {139, 114, 112, 92},
                                              {141, 116, 108, 88},
                                              {145, 120, 100, 80}}};
  const auto actual = c.rects("item");
  for (int i = 0; i < 4; i++)
    check(near(actual[i].x, expected[i].x) &&
              near(actual[i].y, expected[i].y) &&
              near(actual[i].width, expected[i].width) &&
              near(actual[i].height, expected[i].height),
          "C decorated visual rects");
  auto *item = c.result->find_primary_box(c.node("item"));
  check(near(item->own_relative_dx, 15) && near(item->own_relative_dy, -10),
        "C own inset excludes placement");
  c.xy("other", 0, 340);
  // F: extensive normal content dominates scroll extents in both contracts.
  for (auto overflow : {"auto", "hidden"}) {
    fixture f(
        std::string("#toggler{height:80px}#normal{width:500px;height:500px}#"
                    "item{overflow:") +
            overflow + "}",
        "",
        "<div id='normal'></div><div id='fixed'><div id='leaf'></div></div>");
    f.layout(false);
    const auto legacy = f.rects("normal");
    const auto oldMetrics = aether_layout_box_get_scroll_metrics(
        f.result->find_primary_box(f.node("item")));
    f.layout();
    f.xy("fixed", 20, 20);
    const auto current = f.rects("normal");
    const auto metrics = aether_layout_box_get_scroll_metrics(
        f.result->find_primary_box(f.node("item")));
    for (int i = 0; i < 4; i++)
      check(near(legacy[i].x, current[i].x) &&
                near(legacy[i].y, current[i].y) &&
                near(legacy[i].width, current[i].width) &&
                near(legacy[i].height, current[i].height),
            "F normal visual geometry preserved");
    check(
        near(metrics.scrollable_overflow_x, oldMetrics.scrollable_overflow_x) &&
            near(metrics.scrollable_overflow_y,
                 oldMetrics.scrollable_overflow_y),
        "F local scroll extents preserved");
    check(near(metrics.scrollable_overflow_x, 400) &&
              near(metrics.scrollable_overflow_y, 420),
          "F explicit local ranges");
  }
  // Full-map snapshots include HTML and hidden elements before any boxes exist.
  fixture hidden("html{transform:translate(0)}#other{display:none;transform:"
                 "translate(0)}");
  hidden.layout();
  check(hidden.result->effective_fixed_container(
            hidden.node("html")) &&
            hidden.result->effective_fixed_container(
                hidden.node("other")),
        "H complete map domain");
  check(!hidden.prepare(hidden.transitions), "H full-map snapshot stable");
  // Inverse: target transform is present even while its initial matrix is
  // identity.
  fixture inverse;
  inverse.layout();
  inverse.xy("fixed", 20, 20);
  auto before = inverse.style("toggler"), after = before;
  auto op = translate(10);
  after.transforms = {&op, 1};
  after.transition_properties = AETHER_TRANSITION_PROPERTY_TRANSFORM;
  after.transition_duration = .1f;
  aether_transition_runtime_observe(
      inverse.transitions, inverse.node("toggler"), &before, &after, 0);
  // Publish a new immutable legacy version with the same test-only transform
  // override; the retained layout continues to own its previous version.
  auto map_style = before;
  map_style.transforms = {&op, 1};
  immutable_style_test::replace_style(inverse.styles, inverse.node("toggler"), map_style);
  inverse.layout();
  inverse.xy("fixed", 150, 150);
  aether_transition_runtime_tick(inverse.transitions, 100);
  check(!inverse.prepare(inverse.transitions),
        "H inverse completion keeps static context");
  inverse.layout();
  inverse.xy("fixed", 150, 150);
  std::puts("PASS C decorated relative metadata; F raw/scroll ranges; H "
            "full-map and inverse");
}

static void backdrop_value_transitions() {
  fixture f;
  f.layout(); f.xy("fixed",20,20);
  for(const char* value : {"blur()","blur(2px)","blur(calc(0px - 1em))","none","blur()","blur(var(--bad))"}) {
    const std::string source=std::string("--bad:2;backdrop-filter:")+value;
    check(aether_element_set_attribute(f.node("toggler"),"style",source.c_str())==AETHER_OK,"change filter source");
    const auto impact=f.resolve();
    const bool local=std::strcmp(value,"none")!=0 && std::strstr(value,"var(")==nullptr;
    f.layout(); f.xy("fixed",local?150:20,local?150:20);
    if(!local)check((impact&AETHER_STYLE_IMPACT_LAYOUT)!=0,"loss of filter context invalidates layout");
  }
}
static void backdrop_completion_guard() {
  fixture f("#toggler{backdrop-filter:blur(0)}");
  auto before = f.style("toggler"), after = before;
  auto operation = translate(10);
  before.transforms = {&operation, 1};
  after.transition_properties = AETHER_TRANSITION_PROPERTY_TRANSFORM;
  after.transition_duration = .1f;
  aether_transition_runtime_observe(f.transitions, f.node("toggler"), &before,
                                    &after, 0);
  f.layout();
  f.xy("fixed", 150, 150);
  aether_transition_runtime_tick(f.transitions, 100);
  check(!f.prepare(f.transitions),
        "H backdrop retains static context at completion");
  f.layout();
  f.xy("fixed", 150, 150);
  std::puts("PASS H static backdrop remains after transform completion");
}

static void finite_placement_guards() {
  for (const auto *external :
       {"", "#outer{width:100px;flex-direction:column;justify-content:flex-"
            "start}#item{width:160px}"}) {
    fixture f(std::string(external) +
                  "#normal{width:10px;height:10px;margin-left:10px;margin-top:"
                  "10px}#absolute{position:absolute;left:10px;top:10px;width:"
                  "10px;height:10px}",
              "",
              "<div id='fixed'><div id='normal'></div><div "
              "id='absolute'></div></div>");
    f.layout();
    f.xy("fixed", 20, 20);
    f.xy("absolute", 30, 30);
    auto normal = f.rects("normal");
    check(near(normal[0].x, 20) && near(normal[0].y, 20),
          "B normal margin origin");
    for (int i = 1; i < 4; i++)
      check(near(normal[i].x, 30) && near(normal[i].y, 30),
            "B normal inside fixed follows own placement");
  }
  fixture atomic(
      "#outer{display:block;text-align:center}#prefix{display:inline-block;"
      "width:50px;height:20px}#item{display:inline-block;position:relative}#"
      "absolute{position:absolute;left:10px;top:10px;width:10px;height:10px}",
      "",
      "<div id='fixed'><div id='leaf'></div></div><div id='absolute'></div>",
      true);
  atomic.layout();
  atomic.xy("fixed", 20, 20);
  atomic.xy("absolute", 165, 10);
  fixture margins(
      "#outer{justify-content:flex-start;align-items:flex-start}#absolute{"
      "position:absolute;left:10px;top:10px;width:10px;height:10px}",
      "margin-left:auto;position:relative;",
      "<div id='fixed'><div id='leaf'></div></div><div id='absolute'></div>");
  margins.layout();
  margins.xy("fixed", 20, 20);
  margins.xy("absolute", 270, 10);
  fixture zero("#fixed{left:0;top:0}");
  zero.layout();
  zero.xy("fixed", 0, 0);
  fixture resize;
  for (auto size : {aether_sizef{360, 340}, aether_sizef{400, 380},
                    aether_sizef{360, 340}}) {
    resize.layout(true, size.width, size.height);
    resize.xy("fixed", 20, 20);
  }
  std::puts("PASS B normal/absolute own placement; D 50px prefix and auto "
            "margin; E zero and fixed resize");
}

int main() {
  check(aether_abi_version() == 0x00220000 && aether_abi_capabilities() == 0x7f,
        "ABI 0.34 caps7F");
  migration_and_errors();
  static_matrix();
  temporal();
  remaining_guards();
  backdrop_value_transitions();
  backdrop_completion_guard();
  finite_placement_guards();
  std::puts("PASS fixed viewport protocol and finite native guards");
}
