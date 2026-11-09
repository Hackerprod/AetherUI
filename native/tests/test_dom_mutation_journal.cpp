#include "../src/dom/dom_types.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>

static bool fail_alloc = false;
void* operator new(std::size_t n) {
    if (fail_alloc) throw std::bad_alloc();
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::abort(); } } while (0)
using aether::dom::MutationKind;
using aether::dom::JournalLoss;

static aether_doc_s* document() {
    aether_doc_t doc = nullptr;
    CHECK(aether_doc_create(&doc) == AETHER_OK);
    CHECK(aether_doc_parse_html(doc, "<html><body><div></div><p></p></body></html>") == AETHER_OK);
    return doc;
}

int main() {
    auto* doc = document();
    auto* body = aether_doc_get_body(doc);
    auto* child = aether_node_get_child(body, 0);
    auto* sibling = aether_node_get_child(body, 1);
    const auto& journal = doc->mutation_journal;
    CHECK(journal.size() == 1 && journal.begin()->kind == MutationKind::structure);
    CHECK(journal.begin()->subject == doc->identity);
    CHECK(!journal.coverage_lost(doc->identity->mutation_epoch));
    const auto structure = doc->identity->structure_epoch;
    auto epoch = doc->identity->mutation_epoch;
    // No caller list exists: both state changes are captured, including A-B-A.
    fail_alloc = true;
    aether_element_set_state(child, 1);
    aether_element_set_state(sibling, 2);
    aether_element_set_state(child, 0);
    aether_element_set_state(child, 0);
    fail_alloc = false;
    CHECK(doc->identity->mutation_epoch == epoch + 3);
    CHECK(doc->identity->structure_epoch == structure);
    CHECK(journal.size() == 4);
    CHECK(journal.begin()[1].old_state == 0 && journal.begin()[1].new_state == 1);
    CHECK(journal.begin()[2].subject == sibling->identity);
    CHECK(journal.begin()[3].old_state == 1 && journal.begin()[3].new_state == 0);
    CHECK(journal.begin()[1].subject == journal.begin()[3].subject);
    CHECK(aether_element_set_attribute(child, "class", "a") == AETHER_OK);
    CHECK(journal.end()[-1].kind == MutationKind::attribute);
    // Existing internal text edits must declare themselves or become unknown.
    child->text = "text";
    aether_node_touch_typed(child, MutationKind::text);
    CHECK(journal.end()[-1].kind == MutationKind::text);
    fail_alloc = true;
    aether_node_touch(child);
    fail_alloc = false;
    CHECK(journal.loss() == JournalLoss::unknown_mutation);
    CHECK(journal.coverage_lost(doc->identity->mutation_epoch));
    aether_doc_destroy(doc);

    doc = document(); body = aether_doc_get_body(doc);
    child = aether_node_get_child(body, 0);
    auto token = child->identity;
    epoch = doc->identity->mutation_epoch;
    CHECK(aether_node_remove_child(body, child) == AETHER_OK);
    CHECK(doc->identity->mutation_epoch == epoch + 1);
    CHECK(doc->mutation_journal.end()[-1].subject == token);
    CHECK(!token->attached && token->alive);
    CHECK(aether_node_mutation_journal(child) == nullptr);
    aether_element_set_state(child, 4);
    CHECK(doc->identity->mutation_epoch == epoch + 1);
    CHECK(child->identity->mutation_epoch == 1);
    auto* other = document();
    auto* other_body = aether_doc_get_body(other);
    CHECK(aether_node_append_child(other_body, child) == AETHER_OK);
    CHECK(token->attached && aether_node_mutation_journal(child) == &other->mutation_journal);
    CHECK(other->mutation_journal.end()[-1].kind == MutationKind::structure);
    auto other_epoch = other->identity->mutation_epoch;
    CHECK(aether_node_append_child(body, child) == AETHER_OK);
    CHECK(other->identity->mutation_epoch == other_epoch + 1);
    CHECK(other->mutation_journal.end()[-1].subject == token);
    epoch = doc->identity->mutation_epoch;
    CHECK(aether_node_append_child(body, child) == AETHER_OK);
    CHECK(doc->identity->mutation_epoch == epoch + 2); // Legacy same-root count.
    CHECK(aether_doc_parse_html(doc, "<body>replacement</body>") == AETHER_OK);
    CHECK(!token->alive && !token->attached);
    CHECK(doc->mutation_journal.begin()[1].subject == token);
    aether_doc_destroy(other); aether_doc_destroy(doc);

    doc = document(); child = aether_doc_get_body(doc);
    fail_alloc = true;
    for (size_t i = 0; i < aether::dom::MutationJournal::capacity + 10; ++i)
        aether_element_set_state(child, static_cast<int32_t>(i + 1));
    fail_alloc = false;
    CHECK(doc->mutation_journal.size() == aether::dom::MutationJournal::capacity);
    CHECK(doc->mutation_journal.loss() == JournalLoss::overflow);
    CHECK(doc->mutation_journal.last_epoch() == doc->identity->mutation_epoch);
    CHECK(doc->mutation_journal.coverage_lost(doc->identity->mutation_epoch));
    CHECK(child->state == static_cast<int32_t>(aether::dom::MutationJournal::capacity + 10));
    aether_doc_destroy(doc);

    doc = document();
    ++doc->identity->mutation_epoch; // Unrecorded epoch cannot look covered.
    CHECK(doc->mutation_journal.coverage_lost(doc->identity->mutation_epoch));
    aether_element_set_state(aether_doc_get_body(doc), 1);
    CHECK(doc->mutation_journal.loss() == JournalLoss::epoch_gap);
    aether_doc_destroy(doc);
    doc = document();
    doc->identity->mutation_epoch = std::numeric_limits<uint64_t>::max();
    aether_element_set_state(aether_doc_get_body(doc), 1);
    CHECK(doc->identity->mutation_epoch == 0);
    CHECK(doc->mutation_journal.coverage_lost(0));
    aether_doc_destroy(doc);

    // Failed attribute insertion may have partially modified input; the coarse
    // attribute invalidation must already exist. Canonicalization uses SSO here.
    doc = document(); child = aether_doc_get_body(doc);
    epoch = doc->identity->mutation_epoch;
    bool failed = false;
    fail_alloc = true;
    try { (void)aether_element_set_attribute(child, "x", "v"); }
    catch (const std::bad_alloc&) { failed = true; }
    fail_alloc = false;
    CHECK(failed && doc->identity->mutation_epoch == epoch + 1);
    CHECK(doc->mutation_journal.end()[-1].kind == MutationKind::attribute);
    aether_doc_destroy(doc);
    doc = document();
    token = aether_doc_get_body(doc)->identity;
    epoch = doc->identity->mutation_epoch;
    failed = false;
    fail_alloc = true;
    try { (void)aether_doc_parse_html(doc, "<body>new</body>"); }
    catch (const std::bad_alloc&) { failed = true; }
    fail_alloc = false;
    CHECK(failed && doc->identity->mutation_epoch == epoch + 1);
    CHECK(doc->mutation_journal.end()[-1].kind == MutationKind::structure);
    CHECK(!token->alive && !token->attached);
    aether_doc_destroy(doc);
    // Reusing the exact node address cannot reuse the retained authentic token.
    doc = document(); body = aether_doc_get_body(doc);
    alignas(aether_node_s) unsigned char storage[sizeof(aether_node_s)];
    auto* reused = new (storage) aether_node_s();
    reused->type = AETHER_NODE_ELEMENT;
    CHECK(aether_node_append_child(body, reused) == AETHER_OK);
    std::weak_ptr<node_identity_s> old_identity = reused->identity;
    CHECK(aether_node_remove_child(body, reused) == AETHER_OK);
    reused->~aether_node_s();
    CHECK(!old_identity.expired() && !old_identity.lock()->alive);
    reused = new (storage) aether_node_s();
    reused->type = AETHER_NODE_ELEMENT;
    CHECK(reused->identity != old_identity.lock());
    CHECK(aether_node_append_child(body, reused) == AETHER_OK);
    CHECK(aether_node_remove_child(body, reused) == AETHER_OK);
    reused->~aether_node_s();
    aether_doc_destroy(doc);
    CHECK(old_identity.expired()); // Bounded journal owns no historical node chain.
    std::puts("DOM mutation journal: PASS");
}
