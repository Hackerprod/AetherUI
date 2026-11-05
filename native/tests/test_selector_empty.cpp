#include "selector_empty_fixture.h"

using namespace empty_test;

namespace {

void content_matrix() {
    struct Case { const char* html; bool empty; };
    const Case cases[] = {
        {"", true}, {"<!--template slot-->", true}, {"<!---->", true},
        {"<!----><!--one--><!--two-->", true}, {"x", false}, {" ", false},
        {"\t\n", false}, {"<!--slot--> ", false}, {" <!--slot-->", false},
        {"\xc2\xa0", false}, {"<span></span>", false},
        {"<span style='display:none'></span>", false},
        {"<span><!--slot--></span>", false},
        {"<span><b>x</b></span>", false},
        {"<!--first--><span></span><!--last-->", false},
        {"<span></span><!--last-->", false},
        {"<!--first-->x<!--last-->", false}, {"x<!--last-->", false}
    };
    for (const char* selector : {":empty", ":not(:empty)", ":is(:empty)", ":where(:empty)",
                                 ":not(:not(:empty))", ":is(:where(:empty))"}) {
        std::string html = "<body>";
        for (size_t i = 0; i < std::size(cases); ++i)
            html += "<div class='case' id='c" + std::to_string(i) + "'>" + cases[i].html + "</div>";
        html += "</body>";
        Fixture f(html, std::string(".case{opacity:.2}.case") + selector + "{opacity:.8}", 2);
        std::vector<DomSnapshot> originals;
        std::unordered_set<aether_node_t> dom;
        Fixture::collect_dom(aether_doc_get_body(f.doc), dom);
        for (const auto node : dom) originals.emplace_back(node);
        for (int pass = 0; pass < 3; ++pass) {
            f.resolve();
            for (size_t i = 0; i < std::size(cases); ++i) {
                const auto id = "c" + std::to_string(i);
                const bool expected = std::string(selector) == ":not(:empty)" ? !cases[i].empty : cases[i].empty;
                f.expect(f.get(id.c_str()), expected, std::string(selector) + " case " + id + " pass " + std::to_string(pass));
            }
            for (const auto& original : originals) original.verify("content matrix DOM");
        }
    }
}

void generated_content() {
    const char* html = "<body><div id='host'></div><div id='comments'><!--slot--><!----></div>"
                       "<div id='blank'></div><div id='negative'></div><div id='control'></div>"
                       "<div id='parking'><span id='element'></span>x</div></body>";
    const std::vector<std::string> rules = {
        "#host:empty::before{content:'BEFORE';font-size:17px}",
        "#host:empty::after{content:'AFTER';font-size:19px}",
        "#comments:empty::before{content:'BEFORE';font-size:17px}",
        "#comments:empty::after{content:'AFTER';font-size:19px}",
        "#blank::before{content:'';font-size:17px}",
        "#blank:empty::after{content:'AFTER';font-size:19px}",
        "#negative::before{content:'BEFORE';font-size:17px}",
        "#negative:not(:empty)::after{content:'AFTER';font-size:19px}",
        "#control::before{content:'BEFORE';font-size:17px}",
        "#control::after{content:'AFTER';font-size:19px}"
    };
    for (const bool reverse : {false, true}) {
        std::string css;
        for (size_t i = 0; i < rules.size(); ++i) css += rules[reverse ? rules.size() - i - 1 : i];
        Fixture f(html, css, static_cast<int>(rules.size()));
        const auto host = f.get("host");
        const auto comments = f.get("comments");
        const auto parking = f.get("parking");
        const auto element = f.get("element");
        const auto real_text = aether_node_get_child(parking, 1);
        require(aether_node_get_type(real_text) == AETHER_NODE_TEXT && text(real_text) == "x", "real text fixture");
        const DomSnapshot host_snapshot(host), comment_snapshot(comments);
        const DomSnapshot first_comment(aether_node_get_child(comments, 0));
        const DomSnapshot second_comment(aether_node_get_child(comments, 1));
        auto check_stable = [&]() {
            for (int pass = 0; pass < 3; ++pass) {
                f.resolve();
                f.expect_pseudos(host, {"BEFORE", "AFTER"}, "original empty host");
                f.expect_pseudos(comments, {"BEFORE", "AFTER"}, "comment-only host");
                f.expect_pseudos(f.get("blank"), {"", "AFTER"}, "empty generated content");
                f.expect_pseudos(f.get("negative"), {"BEFORE"}, "not empty ignores before");
                f.expect_pseudos(f.get("control"), {"BEFORE", "AFTER"}, "unconditional control");
                host_snapshot.verify("host DOM");
                comment_snapshot.verify("comment host DOM");
                first_comment.verify("nonempty comment");
                second_comment.verify("empty comment");
                require(f.get("host") == host && f.get("comments") == comments, "ID lookups exclude generated copies");
            }
        };
        check_stable();
        for (const auto content : {element, real_text}) {
            for (int cycle = 0; cycle < 2; ++cycle) {
                append(host, content);
                append(comments, content); // Move between hosts, leaving host empty again.
                f.resolve();
                f.expect_pseudos(host, {"BEFORE", "AFTER"}, "source after content move");
                f.expect_pseudos(comments, {}, "real content suppresses both pseudos");
                append(host, content);
                f.resolve();
                f.expect_pseudos(host, {}, "real content suppresses original pseudos");
                f.expect_pseudos(comments, {"BEFORE", "AFTER"}, "comment-only destination restored");
                remove(host, content);
                f.resolve();
                f.expect_pseudos(host, {"BEFORE", "AFTER"}, "remove restores both pseudos");
                append(parking, content); // Keep detached nodes owned for destruction.
                check_stable();
            }
        }
    }
}

void mutation_transitions() {
    Fixture f("<body><div id='a' class='host'><!--a--></div>"
              "<div id='b' class='host'><!--b--></div>"
              "<div id='parking'><span id='element'></span>x</div></body>",
              ".host{opacity:.2}.host:empty{opacity:.8}", 2);
    const auto a = f.get("a"), b = f.get("b"), parking = f.get("parking");
    const auto comment = aether_node_get_child(a, 0), element = f.get("element");
    const auto real_text = aether_node_get_child(parking, 1);
    const DomSnapshot comment_snapshot(comment);
    auto check = [&](bool a_empty, bool b_empty, const char* label) {
        f.resolve();
        f.expect(a, a_empty, std::string(label) + " a");
        f.expect(b, b_empty, std::string(label) + " b");
    };
    check(true, true, "initial");
    for (int cycle = 0; cycle < 2; ++cycle) {
        append(b, comment);
        check(true, true, "move comment");
        require(aether_node_get_child_count(a) == 0 && aether_node_get_child_count(b) == 2, "comment move public counts");
        append(a, comment);
        check(true, true, "restore comment");
        comment_snapshot.verify("restored comment");
        for (const auto content : {element, real_text}) {
            append(a, content);
            check(false, true, "append content");
            append(b, content);
            check(true, false, "move only content");
            remove(b, content);
            check(true, true, "remove last content");
            append(b, content);
            check(true, false, "reappend content");
            append(parking, content);
            check(true, true, "park content");
        }
        append(a, element);
        append(a, real_text);
        check(false, true, "multiple content children");
        remove(a, element);
        check(false, true, "one content child remains");
        append(parking, element);
        remove(a, real_text);
        check(true, true, "last content removed with comment retained");
        append(parking, real_text);
    }
}

void ancestor_and_sibling_dependencies() {
    Fixture f("<body><section id='parent'><div id='host' class='leaf'><!--slot--></div>"
              "<div id='adjacent'></div><i></i><div id='general'></div></section>"
              "<div id='parking'><span id='element'></span>x</div></body>",
              "#parent,#adjacent,#general{opacity:.2}"
              "#parent:has(.leaf:empty){opacity:.8}"
              "#host:empty + #adjacent{opacity:.8}"
              "#host:empty ~ #general{opacity:.8}"
              "#host::before{content:'BEFORE';font-size:17px}", 7);
    // A selector list compiles to one rule per selector, including the base's three.
    const auto host = f.get("host"), parking = f.get("parking");
    const auto element = f.get("element"), real_text = aether_node_get_child(parking, 1);
    auto check = [&](bool empty, const char* label) {
        // Ancestors/other siblings must be included, so always resolve body.
        for (int pass = 0; pass < 3; ++pass) {
            f.resolve();
            for (const char* id : {"parent", "adjacent", "general"})
                f.expect(f.get(id), empty, std::string(label) + " " + id + " pass " + std::to_string(pass));
            f.expect_pseudos(host, {"BEFORE"}, "unconditional leaf pseudo");
        }
    };
    check(true, "comment only");
    for (const auto content : {element, real_text}) {
        for (int cycle = 0; cycle < 2; ++cycle) {
            append(host, content);
            check(false, "leaf now contains content");
            remove(host, content);
            check(true, "leaf empty again");
            append(parking, content);
        }
    }
}

} // namespace

int main() {
    content_matrix();
    generated_content();
    mutation_transitions();
    ancestor_and_sibling_dependencies();
    std::printf("PASS selector-empty: %d checks (active under NDEBUG)\n", checks);
}
