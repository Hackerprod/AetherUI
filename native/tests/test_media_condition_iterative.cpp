#include "style_runtime_types.h"
#include "media_condition_allocation_scope.h"
#include <cstdio>
#include <vector>

namespace {
using node = media_condition_s;
using kind = node::kind_t;
enum value { no, yes, unknown };
int checks = 0;
int failures = 0;

// Observe both a condition and its negation to distinguish unknown from false.
// Do not use assert: these checks must still run in Release/NDEBUG builds.
void check(const node* condition, const aether_style_environment_t& env,
           value expected, const char* label) {
    node negated{};
    negated.kind = kind::not_;
    negated.inner = const_cast<node*>(condition);
    const bool actual = media_condition_matches(condition, &env);
    const bool inverse = media_condition_matches(&negated, &env);
    ++checks;
    if (actual != (expected == yes) || inverse != (expected == no)) {
        ++failures;
        std::fprintf(stderr, "FAIL %s expected=%d match=%d negation=%d\n",
                     label, expected, actual, inverse);
    }
}
}

int main() {
    aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
    node* constants[]{media_condition_never(), media_condition_always(), media_condition_unknown()};
    constexpr value and_table[3][3]{{no,no,no}, {no,yes,unknown}, {no,unknown,unknown}};
    constexpr value or_table[3][3]{{no,yes,unknown}, {yes,yes,yes}, {unknown,yes,unknown}};
    constexpr value not_table[]{yes,no,unknown};
    for (int left = 0; left != 3; ++left) {
        node negated{};
        negated.kind = kind::not_;
        negated.inner = constants[left];
        check(&negated, env, not_table[left], "NOT matrix");
        for (int right = 0; right != 3; ++right) {
            node combined{};
            combined.left = constants[left];
            combined.right = constants[right];
            combined.kind = kind::and_;
            check(&combined, env, and_table[left][right], "AND matrix");
            combined.kind = kind::or_;
            check(&combined, env, or_table[left][right], "OR matrix");
        }
    }
    check(nullptr, env, yes, "null root");
    node with_null{};
    with_null.left = nullptr;
    with_null.right = media_condition_unknown();
    with_null.kind = kind::and_;
    check(&with_null, env, unknown, "null AND unknown");
    with_null.kind = kind::or_;
    check(&with_null, env, yes, "null OR unknown");
    with_null.kind = kind::not_;
    check(&with_null, env, no, "NOT null");

    // Arena ownership, not recursive edge deletion, also handles shared DAGs.
    media_condition_allocation_scope_s arena;
    auto* width = media_condition_parse("(min-width:700px)", nullptr, nullptr, 0, &arena);
    auto* height = media_condition_parse("(max-height:40em)", nullptr, nullptr, 0, &arena);
    auto* scheme = media_condition_parse("(prefers-color-scheme:dark)", nullptr, nullptr, 0, &arena);
    auto* pointer = media_condition_parse("(pointer:fine)", nullptr, nullptr, 0, &arena);
    auto* motion = media_condition_parse("(prefers-reduced-motion:reduce)", nullptr, nullptr, 0, &arena);
    check(width, env, yes, "width above threshold");
    check(height, env, yes, "height with root font");
    check(scheme, env, no, "light scheme");
    check(pointer, env, no, "no pointer");
    check(motion, env, no, "no reduced motion");
    env.viewport_width = 320;
    env.root_font_size = 10;
    env.color_scheme = 1;
    env.pointer_accuracy = 2;
    env.motion_preference = 1;
    check(width, env, no, "width changed");
    check(height, env, no, "root font changed");
    check(scheme, env, yes, "scheme changed");
    check(pointer, env, yes, "pointer changed");
    check(motion, env, yes, "motion changed");

    // Exactly 10,000 real composite nodes, with no factory constant folding.
    // The vector is fixed before linking, and its destruction is nonrecursive.
    std::vector<node> deep(10000);
    node* previous = width;
    for (auto& item : deep) {
        item.kind = kind::and_;
        item.left = previous;
        item.right = width;
        previous = &item;
    }
    check(previous, env, no, "10000 AND nodes inactive");
    env.viewport_width = 800;
    check(previous, env, yes, "10000 AND nodes after environment change");
    for (auto& item : deep) {
        item.kind = kind::or_;
        item.right = media_condition_unknown();
    }
    check(previous, env, yes, "10000 OR nodes true");
    env.viewport_width = 320;
    check(previous, env, unknown, "10000 OR nodes unknown");
    for (auto& item : deep) {
        item.kind = kind::not_;
        item.inner = item.left;
    }
    check(previous, env, no, "10000 NOT nodes false");
    deep.front().inner = media_condition_unknown();
    check(previous, env, unknown, "10000 NOT nodes unknown");

    // Each branch references the same prior subgraph. A nonmemoizing walker
    // would take exponential time, despite the graph having only 10,000 nodes.
    previous = media_condition_unknown();
    for (auto& item : deep) {
        item.kind = kind::or_;
        item.left = previous;
        item.right = previous;
        previous = &item;
    }
    check(previous, env, unknown, "10000 shared DAG nodes");
    deep.front().left = width;
    deep.front().right = width;
    check(previous, env, no, "shared DAG false after leaf change");
    env.viewport_width = 800;
    check(previous, env, yes, "shared DAG true after environment change");

    std::printf("MEDIA iterative checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
