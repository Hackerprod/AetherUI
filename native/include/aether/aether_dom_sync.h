#pragma once
#include <aether/aether.h>
#ifdef __cplusplus
extern "C" {
#endif
#define AETHER_DOM_SYNC_VERSION 1u
/* Every raw input must still be within its live borrow. Retained handles pin
 * storage, never resurrect a retired token. Token reads allocate nothing. */
typedef struct aether_dom_node_lease_s* aether_dom_node_lease_t;
typedef struct aether_dom_snapshot_s* aether_dom_snapshot_t;
typedef struct aether_dom_token_v1_s {
    uint64_t owner_identity, observation_serial, input_epoch, structure_epoch;
    aether_status_t sticky_status;
} aether_dom_token_v1_t;
typedef struct aether_dom_string_v1_s { const char* data; size_t length; } aether_dom_string_v1_t;
typedef struct aether_dom_attribute_v1_s { aether_dom_string_v1_t name, value; } aether_dom_attribute_v1_t;
typedef struct aether_dom_snapshot_node_v1_s {
    aether_node_t node;
    uint64_t identity, owner_identity, disconnect_serial;
    int32_t type, state, alive, attached;
    int64_t parent_index, style_node_id;
    aether_dom_string_v1_t tag, text, authored_value, runtime_value;
    int32_t authored_value_present, runtime_value_present;
    size_t attribute_count, child_count;
    uint64_t state_bit_write_serial[32];
} aether_dom_snapshot_node_v1_t;
AETHER_API uint32_t aether_dom_sync_version(void);
AETHER_API aether_status_t aether_dom_read_node_identity_v1(aether_node_t node,uint64_t* out_identity);
AETHER_API aether_status_t aether_dom_retain_node_v1(aether_node_t node,aether_dom_node_lease_t* out_lease);
AETHER_API void aether_dom_node_lease_destroy_v1(aether_dom_node_lease_t lease);
AETHER_API aether_status_t aether_dom_node_lease_get_v1(aether_dom_node_lease_t lease,aether_node_t* out_node);
AETHER_API aether_status_t aether_dom_read_token_v1(aether_node_t node,aether_dom_token_v1_t* out_token);
/* Includes the complete live registry (attached AND detached), root, and final
 * payloads of explicitly supplied retained retired nodes. Immutable values and
 * storage are owned by snapshot. No user code is called. */
AETHER_API aether_status_t aether_dom_acquire_snapshot_v1(aether_doc_t doc,
    const aether_dom_node_lease_t* known,size_t known_count,aether_dom_snapshot_t* out_snapshot);
AETHER_API void aether_dom_snapshot_destroy_v1(aether_dom_snapshot_t snapshot);
AETHER_API aether_status_t aether_dom_snapshot_token_v1(aether_dom_snapshot_t snapshot,aether_dom_token_v1_t* out_token);
AETHER_API aether_status_t aether_dom_snapshot_count_v1(aether_dom_snapshot_t snapshot,size_t* out_count);
AETHER_API aether_status_t aether_dom_snapshot_node_v1(aether_dom_snapshot_t snapshot,size_t index,aether_dom_snapshot_node_v1_t* out_node);
AETHER_API aether_status_t aether_dom_snapshot_attribute_v1(aether_dom_snapshot_t snapshot,size_t node,size_t attribute,aether_dom_attribute_v1_t* out_attribute);
AETHER_API aether_status_t aether_dom_snapshot_child_v1(aether_dom_snapshot_t snapshot,size_t node,size_t child,size_t* out_index);

/* Typed operation data is borrowed for prepare only. Scalars have an O(1)
 * clean token barrier; only the touched payload/links are staged. */
typedef struct aether_dom_edit_s* aether_dom_edit_t;
typedef enum aether_dom_edit_kind_v1_e {
    AETHER_DOM_SET_ATTRIBUTE=1, AETHER_DOM_REMOVE_ATTRIBUTE=2,
    AETHER_DOM_SET_STATE=3, AETHER_DOM_SET_RUNTIME_VALUE=4,
    AETHER_DOM_SET_TEXT=5, AETHER_DOM_INSERT_CHILD=6,
    AETHER_DOM_REMOVE_CHILD=7, AETHER_DOM_CLEAR_CHILDREN=8,
    AETHER_DOM_INSERT_SUBTREE=9, AETHER_DOM_REPLACE_CHILDREN=10,
    AETHER_DOM_MATERIALIZE_FOREST=11, AETHER_DOM_SET_LEGACY_ATTRIBUTE=12
} aether_dom_edit_kind_v1_t;
typedef struct aether_dom_node_spec_v1_s {
    int32_t type, state;
    int64_t parent_index; /* -1 denotes a root; otherwise preceding spec index. */
    aether_dom_string_v1_t tag, text, runtime_value;
    int32_t runtime_value_present;
    const aether_dom_attribute_v1_t* attributes;
    size_t attribute_count;
    /* MATERIALIZE only: opaque existing subtree. All payload fields must be
       default; parent_index may name only a NEW preceding spec. Existing roots
       must be unique and non-overlapping. Never replays managed input payload. */
    aether_node_t existing_node;
} aether_dom_node_spec_v1_t;
typedef struct aether_dom_edit_spec_v1_s {
    int32_t kind;
    aether_node_t target, child;
    int64_t index; /* -1 = append; otherwise final authored index after extraction. */
    aether_dom_string_v1_t name, value;
    int32_t state, value_present;
    const aether_dom_node_spec_v1_t* nodes;
    size_t node_count;
} aether_dom_edit_spec_v1_t;
AETHER_API aether_status_t aether_dom_prepare_edit_v1(const aether_dom_token_v1_t* expected,
    const aether_dom_edit_spec_v1_t* spec,aether_dom_edit_t* out_edit);
/* Returns result by spec index, including opaque existing roots. New handles
 * are provisional: only retain/identity reads are allowed before commit. */
AETHER_API aether_status_t aether_dom_edit_created_node_v1(aether_dom_edit_t edit,size_t index,aether_node_t* out_node);
AETHER_API aether_status_t aether_dom_edit_validate_v1(aether_dom_edit_t edit);
AETHER_API aether_status_t aether_dom_edit_commit_v1(aether_dom_edit_t edit,aether_dom_token_v1_t* out_token);
AETHER_API void aether_dom_edit_destroy_v1(aether_dom_edit_t edit);
#ifdef __cplusplus
}
#endif
