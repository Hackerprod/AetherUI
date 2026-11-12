#pragma once
#include <string>
// Out-of-line bridge avoids combining legacy CSS selector enum headers with
// style-runtime internals in the capture executable. Both probes use core parsers.
namespace h7 {
bool keyframes(const std::string& name,const std::string& body);
bool declaration(const std::string& property,const std::string& value);
}
