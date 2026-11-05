#include "selector_empty_fixture.h"
#include "../src/dom/dom_types.h"

using namespace empty_test;

int main() {
    // Only this fixture needs private representation access: the public parser
    // skips zero-length text and the ABI intentionally has no text-data setter.
    // All matching still runs through the actual production style runtime.
    Fixture f("<body><div id='alone' class='host'></div>"
              "<div id='comments' class='host'><!--slot--><!----></div></body>",
              ".host{opacity:.2}.host:empty{opacity:.8}", 2);
    for (const char* id : {"alone", "comments"}) {
        const auto host = f.get(id);
        const auto original_count = aether_node_get_child_count(host);
        auto* child = new aether_node_s();
        child->type = AETHER_NODE_TEXT;
        child->style_node_id = f.doc->next_style_node_id++;
        append(host, child);
        for (const char* data : {"", "x", "", " ", "", "\t\n", ""}) {
            child->text = data;
            for (int pass = 0; pass < 3; ++pass) {
                f.resolve();
                f.expect(host, child->text.empty(), std::string(id) + ": internal text transition");
                require(aether_node_get_child_count(host) == original_count + 1, "empty text remains in public children");
                require(aether_node_get_child(host, original_count) == child, "text identity remains unchanged");
                require(aether_node_get_type(child) == AETHER_NODE_TEXT && text(child) == data, "text data/type remains unchanged");
            }
        }
    }
    std::printf("PASS selector-empty-text: %d checks (active under NDEBUG)\n", checks);
}
