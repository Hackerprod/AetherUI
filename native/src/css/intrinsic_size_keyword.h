#pragma once
namespace aether::css {
// Private computed state; never encoded into the public length ABI.
enum class intrinsic_size_keyword { none, min_content, max_content, fit_content };
}
