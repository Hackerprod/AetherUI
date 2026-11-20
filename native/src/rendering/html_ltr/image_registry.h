#pragma once
#include <aether/aether.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include "../native_paint_list.h"

namespace aether::rendering::html_ltr {
// Registro interno de imágenes para B0/B1/B2 (propuesta A1, ciclo 01).
// Clavea por string de fuente (la URL tal cual: "hero.png" != "./hero.png");
// decodifica con avidez en add() y retiene los píxeles BGRA8 premultiplicados
// que luego viajan al comando prep::Image vía el sidecar image_runs.
class ImageRegistry {
 public:
  static constexpr std::size_t kMaxInputBytes = 64u*1024u*1024u; // §2.3
  static constexpr std::size_t kMaxDecodedBytes = 16u*1024u*1024u; // §2.3
  static constexpr std::int32_t kMaxDimension = 8192; // §2.3
  // Decodifica con avidez. Devuelve LIMIT_EXCEEDED sin asignar píxeles si la
  // cabecera ya excede el presupuesto (§2.3); PARSE_FAILED si no decodifica.
  aether_status_t add(std::string source, const std::uint8_t* bytes, std::size_t byte_count);
  std::shared_ptr<const preparation::DecodedImage> find(const std::string& source) const;
  // Adaptador aether_image_dimensions_fn para el builder (user_data = registry).
  static aether_bool_t dimensions(const char* source, aether_sizef* out, void* user_data);
  // Recorre list->commands; por cada DRAW_BACKGROUND_IMAGE con geometría
  // resuelta y fuente registrada, añade NativeImageEntry{index, image} a
  // NativePaintList::image_runs. Sin entrada → snapshot rechaza (§2.2.4).
  void attach(const aether_display_list_t& list, NativePaintList& paint) const;
 private:
  std::unordered_map<std::string, std::shared_ptr<const preparation::DecodedImage>> images_;
};
} // namespace aether::rendering::html_ltr
