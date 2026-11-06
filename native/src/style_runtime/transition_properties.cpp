#include "transition_properties_internal.h"
#include "style_property_registry_internal.h"
#include "builtin_property_parse_utils.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

/* CSS <time>: an optional sign-less number followed by s or ms. A bare zero
   is the only unitless form the spec permits (0 is valid without a unit). */
static bool bi_try_time(const char* src, float& seconds) {
    if (!src || !*src) return false;
    const std::string t = str_trim(src);
    if (t.empty()) return false;
    char* end = nullptr;
    const float value = std::strtof(t.c_str(), &end);
    if (end == t.c_str() || value < 0.0f) return false;
    const std::string unit = str_trim(end);
    if (unit.empty()) {
        if (value != 0.0f) return false;
        seconds = 0.0f;
        return true;
    }
    if (str_iequals(unit, "s")) { seconds = value; return true; }
    if (str_iequals(unit, "ms")) { seconds = value / 1000.0f; return true; }
    return false;
}

/* transition-property longhand: only the v1 keywords map to bits. Any other
   property name is a valid CSS transition-property that names no animatable
   property in this engine, so it normalizes to the empty bitmask (0). */
static bool bi_try_transition_property_keyword(const char* src, std::string& out) {
    if (!src || !*src) return false;
    const std::string t = str_trim(src);
    if (str_iequals(t, "opacity")) { out = std::to_string(AETHER_TRANSITION_PROPERTY_OPACITY); return true; }
    if (str_iequals(t, "transform")) { out = std::to_string(AETHER_TRANSITION_PROPERTY_TRANSFORM); return true; }
    if (str_iequals(t, "all")) {
        out = std::to_string(AETHER_TRANSITION_PROPERTY_OPACITY | AETHER_TRANSITION_PROPERTY_TRANSFORM);
        return true;
    }
    if (str_iequals(t, "none")) { out = "0"; return true; }
    return false;
}

static bool bi_try_transition_timing(const char* src, std::string& out) {
    if (!src || !*src) return false;
    const std::string t = str_trim(src);
    if (str_iequals(t, "linear")) { out = std::to_string(AETHER_TRANSITION_TIMING_LINEAR); return true; }
    if (str_iequals(t, "ease")) { out = std::to_string(AETHER_TRANSITION_TIMING_EASE); return true; }
    if (str_iequals(t, "ease-in")) { out = std::to_string(AETHER_TRANSITION_TIMING_EASE_IN); return true; }
    if (str_iequals(t, "ease-out")) { out = std::to_string(AETHER_TRANSITION_TIMING_EASE_OUT); return true; }
    if (str_iequals(t, "ease-in-out")) { out = std::to_string(AETHER_TRANSITION_TIMING_EASE_IN_OUT); return true; }
    return false;
}

static bool bi_try_transition_time_text(const char* src, std::string& out) {
    float seconds = 0.0f;
    if (!bi_try_time(src, seconds)) return false;
    out = std::to_string(seconds);
    return true;
}

} // namespace

void transition_properties_register_longhands(style_property_registry_s* reg) {
    // ── Transitions ─────────────────────────────────────────────
    // All four longhands are PAINT-impact only: transitions never relayout.
    // Engine initial values deliberately use the computed-style defaults
    // (none / 0s / 0s / linear) rather than the CSS-spec initials
    // (all / ease), so a bare declaration like `transition: 0.3s` still
    // requires an explicit property and timing before anything animates.
    reg->register_property("transition-property", false, STYLE_IMPACT_PAINT, "none",
        [](const char* s, std::string& o) -> bool {
            if (bi_try_transition_property_keyword(s, o)) return true;
            // v1: any other property name is a valid CSS transition target
            // that this engine cannot animate; normalize it to no-op (0).
            o = "0";
            return true;
        },
        [](aether_computed_style_t* st, const std::string& v) {
            st->transition_properties = std::atoi(v.c_str());
        });

    reg->register_property("transition-duration", false, STYLE_IMPACT_PAINT, "0s",
        bi_try_transition_time_text,
        [](aether_computed_style_t* st, const std::string& v) {
            st->transition_duration = std::strtof(v.c_str(), nullptr);
        });

    reg->register_property("transition-delay", false, STYLE_IMPACT_PAINT, "0s",
        bi_try_transition_time_text,
        [](aether_computed_style_t* st, const std::string& v) {
            st->transition_delay = std::strtof(v.c_str(), nullptr);
        });

    reg->register_property("transition-timing-function", false, STYLE_IMPACT_PAINT, "linear",
        bi_try_transition_timing,
        [](aether_computed_style_t* st, const std::string& v) {
            st->transition_timing = std::atoi(v.c_str());
        });
}

void transition_properties_register_shorthands(style_property_registry_s* reg) {
    // transition shorthand: a single transition, NO comma-separated lists in
    // v1. Components appear in any order: [property] [duration] [delay]
    // [timing-function]. Unspecified components reset to the engine initial
    // values (CSS shorthand reset semantics), so `transition: all 0.3s ease`
    // yields properties=OPACITY|TRANSFORM, duration=0.3s, delay=0s,
    // timing=EASE. The writer values are re-parseable CSS tokens so the
    // longhand parsers validate them through the normal cascade path.
    reg->register_shorthand("transition",
        [](const char* source, style_shorthand_writer_s& writer) -> bool {
            const auto format_time = [](float seconds) {
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%gs", static_cast<double>(seconds));
                return std::string(buffer);
            };
            std::string t = str_trim(source);
            if (bi_is_global_keyword(t)) {
                writer.set("transition-property", t.c_str());
                writer.set("transition-duration", t.c_str());
                writer.set("transition-delay", t.c_str());
                writer.set("transition-timing-function", t.c_str());
                return true;
            }
            const auto parts = bi_split_ws(t);
            if (parts.empty()) return false;
            std::string property_value = "none";
            std::string duration_value = "0s";
            std::string delay_value = "0s";
            std::string timing_value = "linear";
            bool has_property = false, has_duration = false, has_delay = false, has_timing = false;
            for (const auto& part : parts) {
                std::string keyword;
                if (!has_property && bi_try_transition_property_keyword(part.c_str(), keyword)) {
                    // Write the KEYWORD token, not the numeric bitmask: the
                    // transition-property longhand re-parses this value through
                    // the normal cascade path, and it only accepts keywords.
                    property_value = part;
                    has_property = true;
                    continue;
                }
                float seconds = 0.0f;
                if (bi_try_time(part.c_str(), seconds)) {
                    if (!has_duration) {
                        duration_value = format_time(seconds);
                        has_duration = true;
                    } else if (!has_delay) {
                        delay_value = format_time(seconds);
                        has_delay = true;
                    } else {
                        return false;
                    }
                    continue;
                }
                if (!has_timing && bi_try_transition_timing(part.c_str(), keyword)) {
                    // Keep the keyword token so the longhand re-parses it.
                    timing_value = part;
                    has_timing = true;
                    continue;
                }
                return false;
            }
            writer.set("transition-property", property_value.c_str());
            writer.set("transition-duration", duration_value.c_str());
            writer.set("transition-delay", delay_value.c_str());
            writer.set("transition-timing-function", timing_value.c_str());
            return true;
        }, {"transition-property", "transition-duration", "transition-delay", "transition-timing-function"});
}
