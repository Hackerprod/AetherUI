#pragma once
#include <cstdint>
#include "dom_lifetime_testing.h"
#include <memory>
#include <array>
struct EditableNodeStorage;
struct DomOwnerControl;
struct DomNodeOwnerPin;
struct DomInteractionClaim;
/* Retained independently of the raw node. Pointer identity is never recycled
 * while any snapshot retains this token. No global node registry is needed. */
struct node_identity_s {
    AETHER_DOM_COUNT_INSTANCE(identity)
    const void* owner = nullptr;
    std::weak_ptr<EditableNodeStorage> storage;
    std::weak_ptr<DomOwnerControl> document_owner;
    std::weak_ptr<DomNodeOwnerPin> live_owner_pin;
    uint64_t disconnect_serial = 0;
    std::array<uint64_t, 32> state_bit_write_serial{};
    std::array<std::weak_ptr<DomInteractionClaim>, 3> interaction_claims;
    bool alive = true;
    bool attached = true;
    uint64_t mutation_epoch = 0;
    uint64_t structure_epoch = 0;
    uint64_t style_tree_epoch = 0;
};
