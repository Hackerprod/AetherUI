#pragma once
#include "typography_evidence.h"
#include <cstdint>
namespace aether::style {
// Owned by the immutable revision's node record. Never authorizes rendering.
struct TypographyRecord {
    TypographyEvidence evidence;
    uint64_t generation=0, document_identity=0, input_epoch=0, structure_epoch=0,
        style_tree_epoch=0, sheet_epoch=0, registry_version=1;
};
using TypographyRecordRef=std::shared_ptr<const TypographyRecord>;
}
