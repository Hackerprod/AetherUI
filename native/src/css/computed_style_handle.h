#pragma once
#include <aether/aether.h>
#include <memory>
#include "frozen_node_record.h"
#include <cstdint>
struct measure_state_s;
class style_revision_s;
struct node_identity_s;
/* One private definition for every producer, clone and destroy translation unit.
 * The flat style owns its buffers only for heap/public handles and state owners;
 * legacy stack adapters may borrow it and must not be freed. Metadata is RAII. */
struct aether_computed_style_handle_s {
    aether_computed_style_t style{};
    aether::style::FrozenMeasuresRef measures;
    aether::style::FrozenNodeRef record;
    std::shared_ptr<const style_revision_s> revision;
    std::shared_ptr<node_identity_s> node_lifetime;
    std::shared_ptr<node_identity_s> document_lifetime;
    uint64_t document_identity = 0;
    uint64_t node_identity = 0;
};
