#include "BookCacheUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>

#include <cstring>
#include <vector>

#include "BookmarkUtil.h"

bool isBookCacheDirectoryName(const char* name) {
  if (!name) {
    return false;
  }

  constexpr char EPUB_PREFIX[] = "epub_";
  constexpr char TXT_PREFIX[] = "txt_";
  constexpr char XTC_PREFIX[] = "xtc_";

  return strncmp(name, EPUB_PREFIX, std::size(EPUB_PREFIX) - 1) == 0 ||
         strncmp(name, TXT_PREFIX, std::size(TXT_PREFIX) - 1) == 0 ||
         strncmp(name, XTC_PREFIX, std::size(XTC_PREFIX) - 1) == 0;
}

std::string bookCachePath(const std::string& path) {
  // The book classes only hash the path in their constructors (no file IO).
  if (FsHelpers::hasEpubExtension(path)) {
    return Epub(path, "/.crosspoint").getCachePath();
  }
  if (FsHelpers::hasXtcExtension(path)) {
    return Xtc(path, "/.crosspoint").getCachePath();
  }
  if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    return Txt(path, "/.crosspoint").getCachePath();
  }
  return std::string();
}

namespace {
// Bookmarks are keyed by the book path as well (EPUB only).
void moveBookmarks(const std::string& oldBook, const std::string& newBook) {
  if (!FsHelpers::hasEpubExtension(oldBook)) {
    return;
  }
  const std::string oldMarks = BookmarkUtil::getBookmarkPath(oldBook);
  const std::string newMarks = BookmarkUtil::getBookmarkPath(newBook);
  if (oldMarks == newMarks || !Storage.exists(oldMarks.c_str())) {
    return;
  }
  if (Storage.exists(newMarks.c_str()) || !Storage.rename(oldMarks.c_str(), newMarks.c_str())) {
    LOG_ERR("BookCache", "Failed to move bookmarks %s -> %s", oldMarks.c_str(), newMarks.c_str());
  }
}

// Section files embed absolute image paths under the old cache dir, so after a move the layout
// (sections/ and img_* files) is stale. book.bin, progress.bin and covers are path-independent.
void dropEpubLayout(const std::string& cacheDir) {
  const std::string sectionsDir = cacheDir + "/sections";
  if (Storage.exists(sectionsDir.c_str()) && !Storage.removeDir(sectionsDir.c_str())) {
    LOG_ERR("BookCache", "Failed to drop sections: %s", sectionsDir.c_str());
  }
  HalFile dir = Storage.open(cacheDir.c_str());
  if (!dir || !dir.isDirectory()) {
    return;
  }
  // Collect first: removing while iterating can skip entries.
  std::vector<std::string> stale;
  char name[128];
  HalFile entry = dir.openNextFile();
  while (entry) {
    name[0] = '\0';
    entry.getName(name, sizeof(name));
    if (!entry.isDirectory() && strncmp(name, "img_", 4) == 0) {
      stale.emplace_back(cacheDir + "/" + name);
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();
  for (const auto& path : stale) {
    if (!Storage.remove(path.c_str())) {
      LOG_ERR("BookCache", "Failed to drop image: %s", path.c_str());
    }
  }
}
}  // namespace

void moveBookCache(const char* oldPath, const char* newPath) {
  if (!oldPath || !newPath) {
    return;
  }
  const std::string oldBook(oldPath);
  const std::string newBook(newPath);
  moveBookmarks(oldBook, newBook);

  const std::string oldCache = bookCachePath(oldBook);
  const std::string newCache = bookCachePath(newBook);
  if (oldCache.empty() || oldCache == newCache || !Storage.exists(oldCache.c_str())) {
    return;
  }
  if (newCache.empty()) {
    // Renamed to an unsupported extension: the old cache is orphaned.
    clearBookCache(oldBook);
    return;
  }
  if (Storage.exists(newCache.c_str())) {
    LOG_ERR("BookCache", "Cache target exists, dropping caches: %s", newCache.c_str());
    clearBookCache(oldBook);
    clearBookCache(newBook);
    return;
  }
  if (!Storage.rename(oldCache.c_str(), newCache.c_str())) {
    LOG_ERR("BookCache", "Failed to move cache %s -> %s", oldCache.c_str(), newCache.c_str());
    clearBookCache(oldBook);
    return;
  }
  if (FsHelpers::hasEpubExtension(newBook)) {
    dropEpubLayout(newCache);
  }
}

void clearBookCache(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) {
    Epub(path, "/.crosspoint").clearCache();
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc(path, "/.crosspoint").clearCache();
  } else if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    Txt(path, "/.crosspoint").clearCache();
  } else {
    return;
  }
  LOG_DBG("BookCache", "Done checking metadata cache for: %s", path.c_str());
}
