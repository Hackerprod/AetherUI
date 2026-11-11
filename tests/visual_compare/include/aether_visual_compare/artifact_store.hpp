#pragma once
#include "resource_validation.hpp"
namespace avc {
bool prepare_output(FileSystem&,const Request&);
// Final cancellation sample occurs here, before serialization. No sample after it.
// Returns 13 on any publication failure; candidates are never repaired or retried.
int commit_report(FileSystem&,Report&,const CancellationToken&);
// Consumers must validate marker plus both closed reports, not candidate status.
bool verify_committed(std::string_view marker,std::string_view report_json,std::string_view report_log);
}
