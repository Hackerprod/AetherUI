#pragma once
#include <aether/aether.h>
#ifdef __cplusplus
extern "C" {
#endif
/* External root ownership, never held by a DOM node or registry. Protect this
 * handle against concurrent release during every call using the borrowed doc.
 * Ordinary DOM operations and explicit retirement remain owner-thread only. */
typedef struct aether_dom_owner_s* aether_dom_owner_t;
AETHER_API aether_status_t aether_dom_owner_create_v1(
    aether_dom_owner_t* out_owner, aether_doc_t* out_document);
/* Owner-thread logical disposal; retained derivatives see dead tokens.
 * Idempotent while this ownership handle remains valid. */
AETHER_API aether_status_t aether_dom_owner_retire_v1(aether_dom_owner_t owner);
/* ANY thread, once per handle. Drops ownership only, without logically retiring
 * a document retained by derivatives. No callbacks or dispatch queues. The
 * caller must exclude concurrent use/release of this same handle. */
AETHER_API void aether_dom_owner_release_v1(aether_dom_owner_t owner);
#ifdef __cplusplus
}
#endif
