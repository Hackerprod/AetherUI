#pragma once
#include <cstdint>
/* Retained independently of the raw node. Pointer identity is never recycled
 * while any snapshot retains this token. No global node registry is needed. */
struct node_identity_s {
    const void* owner = nullptr;
    bool alive = true;
    bool attached = true;
    uint64_t mutation_epoch = 0;
    // Never wraps: exhaustion permanently disqualifies provenance reuse.
    uint64_t non_state_epoch = 0;
    bool non_state_exhausted = false;
    uint64_t structure_epoch = 0;
    uint64_t style_tree_epoch = 0;
};
