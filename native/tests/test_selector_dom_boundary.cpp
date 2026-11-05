#include "style_runtime_types.h"
#include "../src/dom/dom_types.h"

#include <cstdio>
#include <cstdlib>

// Private boundary checks for malformed/synthetic nodes that the public DOM
// cannot construct. The ABI regressions exercise actual style resolution.
namespace {
int checks = 0;
int failures = 0;

void expect(const char* source, aether_node_s* subject, bool expected) {
    compiled_selector_s selector;
    if (!selector_parse(source, selector)) {
        std::fprintf(stderr, "HARNESS: rejected selector %s\n", source);
        std::exit(2);
    }
    ++checks;
    if (selector_match(selector, subject) != expected) {
        ++failures;
        std::fprintf(stderr, "FAIL %s expected %s\n", source, expected ? "match" : "no match");
    }
}

aether_node_s element(const char* id) {
    aether_node_s node;
    node.type = AETHER_NODE_ELEMENT;
    node.tag_name = "div";
    node.attributes["id"] = id;
    return node;
}

void append(aether_node_s& parent, aether_node_s& child) {
    child.parent = &parent;
    parent.children.push_back(&child);
}
}

int main() {
    auto outer = element("outer");
    auto host = element("host");
    auto target = element("target");
    auto before = element("stale-pseudo-id");
    auto after = element("stale-pseudo-id");
    append(outer, host);
    for (auto* generated : {&before, &after}) {
        generated->is_generated = true;
        generated->style_origin = &host;
    }
    before.pseudo_element = aether_pseudo_element_t::before;
    after.pseudo_element = aether_pseudo_element_t::after;
    append(host, before);
    append(host, target);
    append(host, after);

    // Generated traversal candidates must not become their host. Subject
    // normalization remains necessary for legitimate pseudo style prefixes.
    for (auto* subject : {&host, &before, &after}) {
        expect("#host:first-child:last-child:only-child:nth-child(1)", subject, true);
        expect("#outer > #host", subject, true);
        expect("#stale-pseudo-id", subject, false);
    }
    for (const char* source : {"#target:first-child", "#target:last-child",
                               "#target:only-child", "#target:nth-child(1)"})
        expect(source, &target, true);
    for (const char* source : {"#target:nth-child(2)", "#host + #target", "#host ~ #target"})
        expect(source, &target, false);

    host.attributes["class"] = "current";
    host.attributes["data-current"] = "yes";
    host.state = AETHER_STATE_HOVER;
    for (auto* subject : {&before, &after})
        expect(".current[data-current=yes]:hover:first-child", subject, true);

    // A generated subtree is pruned as a whole, even if its internal children
    // are not individually flagged. Non-DOM layout wrappers are also excluded.
    auto generated_descendant = element("hidden-generated-descendant");
    append(before, generated_descendant);
    expect("#outer:has(#hidden-generated-descendant)", &outer, false);
    expect("#outer:has(#host:has(#hidden-generated-descendant))", &outer, false);
    expect("#outer:has(#target:first-child)", &outer, true);
    expect("#outer:has(#host + #target)", &outer, false);

    auto wrapper = element("wrapper");
    wrapper.style_origin = &host;
    wrapper.anonymous_flex_owner = &host;
    expect("*", &wrapper, false);
    expect("#host", &wrapper, false);
    expect(":nth-child(1)", &wrapper, false);
    auto ordinary_with_origin = element("ordinary");
    ordinary_with_origin.style_origin = &host;
    expect("#ordinary", &ordinary_with_origin, true);
    expect("#host", &ordinary_with_origin, false);

    // Reject invalid pseudo origins instead of silently treating a synthetic
    // node as a DOM subject, or recursively chasing an origin cycle.
    auto invalid = element("invalid");
    invalid.is_generated = true;
    invalid.pseudo_element = aether_pseudo_element_t::before;
    for (auto* origin : {static_cast<aether_node_s*>(nullptr), &invalid, &before, &wrapper}) {
        invalid.style_origin = origin;
        expect("*", &invalid, false);
    }
    invalid.style_origin = &host;
    invalid.pseudo_element = aether_pseudo_element_t::none;
    expect("*", &invalid, false);
    invalid.pseudo_element = aether_pseudo_element_t::before;
    invalid.type = AETHER_NODE_TEXT;
    expect("*", &invalid, false);
    expect("*", nullptr, false);

    // Missing membership must not turn index -1 into CSS index 0, nor give a
    // non-member sibling matches merely because its parent has other children.
    auto missing = element("missing");
    missing.parent = &host;
    for (const char* source : {":first-child", ":last-child", ":only-child",
                               ":nth-child(0)", ":nth-child(n)", ":nth-child(-n+2)",
                               "#target + *", "#target ~ *"})
        expect(source, &missing, false);

    // Retain the existing disconnected/document-root behavior. This fix is
    // not a change to the grammar or to the disconnected-node compatibility.
    auto detached = element("detached");
    for (const char* source : {":first-child", ":last-child", ":nth-child(1)", ":root"})
        expect(source, &detached, true);
    expect(":only-child", &detached, false);
    expect(":nth-child(0)", &detached, false);
    aether_doc_s doc;
    append(doc, detached);
    expect(":first-child:last-child:only-child:nth-child(1):root", &detached, true);
    expect("*", &doc, false);

    std::printf("selector-dom-boundary: %d checks, %d failures (active under NDEBUG)\n", checks, failures);
    return failures ? 1 : 0;
}
