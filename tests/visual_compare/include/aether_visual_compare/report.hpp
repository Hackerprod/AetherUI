#pragma once
#include "model.hpp"
namespace avc {
// Serialization accepts only the closed vocabulary and validated scalar fields.
// Invalid reports throw invalid_argument with a constant, non-sensitive message.
bool valid_report(const Report&);
std::string serialize_report(const Report&);
std::string serialize_log(const Report&);
}
