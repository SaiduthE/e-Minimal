#pragma once

#include <string>

// Clears the reading cache for a book file if its extension is recognised
// (EPUB, XTC, or TXT). Does nothing for other file types.
void clearBookCache(const std::string& path);

// Cache directory for a book path ("" for unsupported extensions). Path hashing only, no file IO.
std::string bookCachePath(const std::string& path);

// Moves the book's cache dir (reading progress, sections, covers) and EPUB bookmarks from the cache
// keyed by oldPath to the one keyed by newPath. No-op if there is no old cache; if the target exists
// or the move fails, logs and drops the stale caches instead. Uses paths only: call before or after
// the file itself is renamed.
void moveBookCache(const char* oldPath, const char* newPath);

// Returns true if the directory name matches a book cache entry.
bool isBookCacheDirectoryName(const char* name);
