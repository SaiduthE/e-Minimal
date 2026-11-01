#pragma once

#include <map>
#include <string>

#include <BookOverrides.h>

#include "HalStorage.h"

struct FakeMetadata {
  std::string title = "Title";
  std::string author = "Author";
  std::string series;
  std::string seriesIndex;
  std::string genre;
  bool success = true;
};

// No overrides on the fake card unless a test fills this in (keyed by book path).
inline std::map<std::string, bookmeta::BookOverride> bookOverrides;
namespace bookmeta {
inline bool load(const char* bookPath, BookOverride& out) {
  out = BookOverride{};
  const auto it = bookOverrides.find(bookPath);
  if (it == bookOverrides.end()) return false;
  out = it->second;
  return true;
}
}  // namespace bookmeta

inline std::map<std::string, FakeMetadata> bookMetadata;

class Epub {
  std::string path;

 public:
  Epub(const std::string& path, const char*) : path(path) {}

  bool loadMetadata(std::string& title, std::string& author) {
    std::string series, seriesIndex, genre;
    return loadMetadata(title, author, series, seriesIndex, genre);
  }

  bool loadMetadata(std::string& title, std::string& author, std::string& series, std::string& seriesIndex,
                    std::string& genre) {
    ++fake::parses;
    const auto& metadata = bookMetadata[path];
    if (!metadata.success) return false;
    title = metadata.title;
    author = metadata.author;
    series = metadata.series;
    seriesIndex = metadata.seriesIndex;
    genre = metadata.genre;
    return true;
  }
};
