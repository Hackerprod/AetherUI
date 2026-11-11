#pragma once
#include "model.hpp"
#include <filesystem>
#include <memory>
#include <string_view>
namespace avc {
namespace fs = std::filesystem;
enum class FileKind { missing, directory, regular, symlink, special, error };
struct FileInfo { FileKind kind=FileKind::error; std::uint64_t size=0; };
class Reader { public: virtual ~Reader()=default; virtual bool read(char*,std::size_t,std::size_t&)=0; };
class Writer { public: virtual ~Writer()=default; virtual bool write(std::string_view)=0; virtual bool flush()=0; virtual bool close()=0; };
// Filesystem boundary is injectable. No implementation promises race-free opens.
class FileSystem {
public:
 virtual ~FileSystem()=default;
 virtual FileInfo stat(const fs::path&)=0; // Must not follow links.
 virtual std::optional<fs::path> canonical(const fs::path&)=0;
 virtual std::unique_ptr<Reader> open_read(const fs::path&)=0;
 virtual bool create_directory(const fs::path&)=0; // Exclusive: false if exists.
 virtual std::unique_ptr<Writer> open_write(const fs::path&)=0;
 virtual bool rename(const fs::path&,const fs::path&)=0;
};
std::unique_ptr<FileSystem> native_filesystem();
// Lexical only, no filesystem accesses. Absolute portable paths only.
bool valid_local_path(std::string_view);
bool directory_chain(FileSystem&,const fs::path&);
void inspect_resources(FileSystem&,const Request&,Report&,const CancellationToken&);
}
