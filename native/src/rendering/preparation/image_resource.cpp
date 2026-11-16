#include <aether/rendering/preparation/image_resource.hpp>
#include <new>
#include <unordered_set>
namespace aether::rendering::preparation {
ImageResult ImageResource::copy(std::uint32_t w, std::uint32_t h,
                                std::span<const std::uint8_t> p) noexcept {
 constexpr std::size_t cap = 256u*1024u*1024u;
 if (!w || !h || w>16384 || h>16384) return ImageError::InvalidInput;
 if (std::size_t(w)>cap/4/h) return ImageError::LimitExceeded;
 const auto bytes=std::size_t(w)*4*h;
 if (p.size()!=bytes || !p.data()) return ImageError::InvalidInput;
 for (std::size_t i=0;i<bytes;i+=4)
  if(p[i]>p[i+3] || p[i+1]>p[i+3] || p[i+2]>p[i+3]) return ImageError::InvalidInput;
 try { return ImageRef(new ImageResource(w,h,p)); }
 catch(const std::bad_alloc&) { return ImageError::OutOfMemory; }
}
ImageSetResult ImageSet::create(std::span<const ImageRef> refs,
 std::size_t max_bytes, std::size_t max_unique) noexcept {
 // Bound admission work even when an input repeats one resource many times.
 if(max_bytes>256u*1024u*1024u || max_unique>4096 || refs.size()>65536) return ImageError::LimitExceeded;
 try {
  std::unordered_set<const ImageResource*> seen;
  std::vector<ImageRef> unique;
  std::size_t bytes=0;
  for(const auto& ref:refs) {
   if(!ref) return ImageError::InvalidInput;
   if(seen.find(ref.get())!=seen.end()) continue;
   const auto size=ref->pixels().size();
   if(unique.size()>=max_unique || size>max_bytes-bytes) return ImageError::LimitExceeded;
   seen.insert(ref.get()); unique.push_back(ref); bytes+=size;
  }
  return std::shared_ptr<const ImageSet>(new ImageSet(std::move(unique),bytes));
 } catch(const std::bad_alloc&) { return ImageError::OutOfMemory; }
}
}
