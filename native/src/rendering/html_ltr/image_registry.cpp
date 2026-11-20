#include "image_registry.h"
#include "aether/rendering/preparation/paint_snapshot.hpp"
#include "../display_list_types.h"
#include "../../../third_party/stb_image.h"
#include <cstdint>
#include <utility>
#include <vector>

namespace aether::rendering::html_ltr {
aether_status_t ImageRegistry::add(std::string source, const std::uint8_t* bytes, std::size_t byte_count) {
 if(source.empty() || !bytes) return AETHER_ERROR_INVALID_ARGUMENT;
 if(byte_count > kMaxInputBytes) return AETHER_ERROR_LIMIT_EXCEEDED;
 // Pre-chequeo sin asignar (§2.3): la cabecera ya dice si excede el presupuesto.
 int w = 0, h = 0, comp = 0;
 if(!stbi_info_from_memory(bytes, int(byte_count), &w, &h, &comp)) return AETHER_ERROR_PARSE_FAILED;
 if(w < 1 || h < 1 || w > kMaxDimension || h > kMaxDimension ||
    std::uint64_t(w)*std::uint64_t(h)*4u > kMaxDecodedBytes)
  return AETHER_ERROR_LIMIT_EXCEEDED;
 std::int32_t dw = 0, dh = 0; std::uint8_t* pixels = nullptr;
 const auto status = aether_image_decode(bytes, byte_count, &dw, &dh, &pixels);
 if(status != AETHER_OK) return status; // PARSE_FAILED si no decodifica
 // Defensa en profundidad: el decode real manda sobre la cabecera.
 if(!pixels || dw < 1 || dh < 1 || dw > kMaxDimension || dh > kMaxDimension ||
    dw != w || dh != h) {
  if(pixels) aether_image_pixels_free(pixels);
  return AETHER_ERROR_LIMIT_EXCEEDED;
 }
 const std::size_t count = std::size_t(dw)*std::size_t(dh)*4u;
 if(count > kMaxDecodedBytes) { aether_image_pixels_free(pixels); return AETHER_ERROR_LIMIT_EXCEEDED; }
 auto image = std::make_shared<preparation::DecodedImage>();
 image->width = dw; image->height = dh;
 image->pixels.assign(pixels, pixels + count);
 aether_image_pixels_free(pixels);
 images_[std::move(source)] = std::move(image);
 return AETHER_OK;
}

std::shared_ptr<const preparation::DecodedImage> ImageRegistry::find(const std::string& source) const {
 const auto it = images_.find(source);
 return it == images_.end() ? nullptr : it->second;
}

aether_bool_t ImageRegistry::dimensions(const char* source, aether_sizef* out, void* user_data) {
 if(!source || !out || !user_data) return AETHER_FALSE;
 const auto* self = static_cast<const ImageRegistry*>(user_data);
 const auto image = self->find(source);
 if(!image) return AETHER_FALSE;
 out->width = float(image->width);
 out->height = float(image->height);
 return AETHER_TRUE;
}

void ImageRegistry::attach(const aether_display_list_t& list, NativePaintList& paint) const {
 if(!list) return;
 for(std::size_t i = 0; i < list->commands.size(); ++i) {
  const auto& cmd = list->commands[i];
  if(cmd.tag != AETHER_PAINT_DRAW_BACKGROUND_IMAGE) continue;
  const auto& d = cmd.data.draw_background_image;
  if(!d.has_resolved_geometry || !d.source) continue;
  const auto image = find(d.source);
  if(!image) continue; // Sin entrada → snapshot rechaza con UNSUPPORTED_TAG (§2.2.4 regla 2).
  paint.image_runs.push_back(NativeImageEntry{i, image});
 }
}
} // namespace aether::rendering::html_ltr
