#pragma once
#include "typography_evidence.h"
#include "style_source_accounting.h"
#include "../rendering/text/native_paragraph_diagnostic.h"
#include <cstdint>
namespace aether::style {
// Owned by the immutable revision's node record. Never authorizes rendering.
struct TypographyRecord {
    TypographyEvidence evidence;
    std::shared_ptr<const style_source_accounting_s> inputs;
    aether::rendering::text::ParagraphDiagnosticRef text_diagnostic;
    // Independent prerequisites, not alternatives. Neither is established here.
    const bool context_unproven=true, font_unresolved=true;
    uint64_t generation=0, document_identity=0, input_epoch=0, structure_epoch=0,
        style_tree_epoch=0, sheet_epoch=0, registry_version=1;
};
using TypographyRecordRef=std::shared_ptr<const TypographyRecord>;
}
