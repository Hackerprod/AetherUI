#pragma once
#include "pango_request_conversion.h"
#include <cairo.h>
#include <pango/pangocairo.h>
#include <memory>
#include <thread>
namespace aether::rendering::text {
class PangoLayoutOwner {
 Request request_;
 std::thread::id owner_{std::this_thread::get_id()};
 PangoContext* context_{};
 PangoLayout* layout_{};
 explicit PangoLayoutOwner(const Request& r):request_(r) {}
public:
 PangoLayoutOwner(const PangoLayoutOwner&)=delete;
 PangoLayoutOwner& operator=(const PangoLayoutOwner&)=delete;
 PangoLayoutOwner(PangoLayoutOwner&&)=delete;
 PangoLayoutOwner& operator=(PangoLayoutOwner&&)=delete;
 // Wrong-thread destruction terminates before invoking native releases.
 ~PangoLayoutOwner();
 using CreateResult=std::variant<std::unique_ptr<PangoLayoutOwner>,PangoError>;
 static CreateResult create(const Request&) noexcept;
 MetricsResult metrics() const noexcept;
 // Borrowed target; failures may leave partial pixels (no rollback).
 std::optional<PangoError> draw(cairo_t*,double x,double y,prep::Color) const noexcept;
};
}
