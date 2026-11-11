#pragma once
#include "resource_validation.hpp"
#include <span>
#include <ostream>
namespace avc {
std::optional<Request> parse_cli(std::span<const std::string_view>);
// args excludes executable name. The run-id provider is injectable and may throw.
int run_cli(std::span<const std::string_view>, FileSystem&, const CancellationToken&,
            const std::function<std::string()>&, std::ostream&, std::ostream&);
}
