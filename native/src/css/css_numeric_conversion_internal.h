#pragma once

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <locale.h>
#include <string>
#if defined(__APPLE__)
#include <xlocale.h>
#endif

namespace aether::css {

template<class T> struct numeric_conversion_result {
    T value{};
    bool valid = false;
    bool range_error = false;
};

// One privately owned C numeric locale per parse, acquired only at the first
// conversion. Failure is sticky for this owner: never retry within an expression
// or fall back to the caller's process/thread locale.
class c_numeric_locale {
public:
    c_numeric_locale() = default;
    ~c_numeric_locale() {
        if (!locale_) return;
#if defined(_WIN32)
        _free_locale(locale_);
#else
        freelocale(locale_);
#endif
    }
    c_numeric_locale(const c_numeric_locale&) = delete;
    c_numeric_locale& operator=(const c_numeric_locale&) = delete;
    c_numeric_locale(c_numeric_locale&&) = delete;
    c_numeric_locale& operator=(c_numeric_locale&&) = delete;

    numeric_conversion_result<float> parse_float(const std::string& text) {
        if (!acquire()) return {};
        char* end = nullptr;
        errno = 0;
#if defined(_WIN32)
        const float value = _strtof_l(text.c_str(), &end, locale_);
#else
        const float value = strtof_l(text.c_str(), &end, locale_);
#endif
        const int conversion_errno = errno;
        return validate(text, end, value, conversion_errno);
    }

    numeric_conversion_result<double> parse_double(const std::string& text) {
        if (!acquire()) return {};
        char* end = nullptr;
        errno = 0;
#if defined(_WIN32)
        const double value = _strtod_l(text.c_str(), &end, locale_);
#else
        const double value = strtod_l(text.c_str(), &end, locale_);
#endif
        const int conversion_errno = errno;
        return validate(text, end, value, conversion_errno);
    }

private:
    bool acquire() {
        if (!attempted_) {
            attempted_ = true;
#if defined(_WIN32)
            locale_ = _create_locale(LC_NUMERIC, "C");
#else
            locale_ = newlocale(LC_NUMERIC_MASK, "C", nullptr);
#endif
        }
        return locale_ != nullptr;
    }

    template<class T> static numeric_conversion_result<T> validate(
        const std::string& text, const char* end, T value, int conversion_errno) {
        const bool valid = end != text.c_str() && end == text.c_str() + text.size() &&
                           std::isfinite(value);
        // Keep policy at the caller: RGB allows finite ERANGE, lengths do not.
        return {valid ? value : T{}, valid, conversion_errno == ERANGE};
    }

#if defined(_WIN32)
    _locale_t locale_ = nullptr;
#else
    locale_t locale_ = nullptr;
#endif
    bool attempted_ = false;
};

} // namespace aether::css
