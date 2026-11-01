#pragma once

#include <cstdint>

// User overrides for a book's own (EPUB) metadata, one JSON file per book under
// /.crosspoint/meta/<16 hex of clixPathHash(path)>.json. An empty field means "not overridden".
//
// A BookOverride is ~400 bytes: callers must hold it in a member or on the heap
// (makeUniqueNoThrow<BookOverride>()), never as a local in a deep call chain.
//
// Every mutation marks the library index dirty and appends the affected path hashes to
// /.crosspoint/meta/reenrich.bin (8-byte little-endian uint64 each), so the index builder re-reads those
// books even though their file size and mtime did not change.
namespace bookmeta {

inline constexpr uint16_t NO_SERIES_INDEX = 0xFFFF;

struct BookOverride {
  char title[128];
  char author[96];
  char series[96];
  char seriesIndex[12];
  char genre[64];
};

// false = no override file (out is zeroed).
bool load(const char* bookPath, BookOverride& out);
// All fields empty removes the file.
bool save(const char* bookPath, const BookOverride& in);
bool remove(const char* bookPath);
// Re-keys the override of a single book file; true when there was nothing to move.
bool move(const char* oldPath, const char* newPath);
// Re-keys every override whose book lived under oldDir.
void moveTree(const char* oldDir, const char* newDir);
// Drops every override whose book lived under dir.
void removeTree(const char* dir);

// "2" -> 200, "2.5" -> 250, "2.55" -> 255 (hundredths); invalid, empty or out of range -> NO_SERIES_INDEX.
inline uint16_t parseSeriesIndex(const char* s) {
  if (s == nullptr) return NO_SERIES_INDEX;
  while (*s == ' ') s++;
  if (*s < '0' || *s > '9') return NO_SERIES_INDEX;
  uint32_t whole = 0;
  while (*s >= '0' && *s <= '9') {
    whole = whole * 10 + static_cast<uint32_t>(*s - '0');
    if (whole > 600) return NO_SERIES_INDEX;
    s++;
  }
  uint32_t frac = 0;
  if (*s == '.' || *s == ',') {
    s++;
    if (*s < '0' || *s > '9') return NO_SERIES_INDEX;
    frac = static_cast<uint32_t>(*s - '0') * 10;
    s++;
    if (*s >= '0' && *s <= '9') {
      frac += static_cast<uint32_t>(*s - '0');
      s++;
    }
    while (*s >= '0' && *s <= '9') s++;  // finer precision is dropped
  }
  while (*s == ' ') s++;
  if (*s != '\0') return NO_SERIES_INDEX;
  const uint32_t value = whole * 100 + frac;
  return value >= NO_SERIES_INDEX ? NO_SERIES_INDEX : static_cast<uint16_t>(value);
}

}  // namespace bookmeta
