#include "BookOverrides.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <LibraryFormat.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace bookmeta {
namespace {

constexpr char META_DIR[] = "/.crosspoint/meta";
constexpr char REENRICH_PATH[] = "/.crosspoint/meta/reenrich.bin";
constexpr size_t HASH_NAME_LEN = 16;  // hex digits before ".json"

uint64_t pathHash(const char* path) { return library::clixPathHash(path, strlen(path)); }

void hashToPath(const uint64_t hash, char (&out)[48]) {
  snprintf(out, sizeof(out), "%s/%016llx.json", META_DIR, static_cast<unsigned long long>(hash));
}

// Appends the hashes to reenrich.bin and marks the library index dirty.
void noteChanged(const uint64_t* hashes, const size_t count) {
  library::markLibraryIndexDirty();
  if (count == 0) return;
  if (!Storage.exists(META_DIR) && !Storage.mkdir(META_DIR)) {
    LOG_ERR("BMETA", "cannot create %s", META_DIR);
    return;
  }
  HalFile file = Storage.open(REENRICH_PATH, O_WRITE | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("BMETA", "cannot open reenrich list");
    return;
  }
  for (size_t i = 0; i < count; i++) {
    uint8_t le[8];
    for (int b = 0; b < 8; b++) le[b] = static_cast<uint8_t>(hashes[i] >> (8 * b));
    if (file.write(le, sizeof(le)) != sizeof(le)) {
      LOG_ERR("BMETA", "short write to reenrich list");
      return;
    }
  }
}

void copyField(char* dst, const size_t cap, const char* src) {
  if (src == nullptr) src = "";
  snprintf(dst, cap, "%s", src);
}

void setIfNonEmpty(JsonDocument& doc, const char* key, const char* value) {
  if (value[0] != '\0') doc[key] = value;
}

bool hasAnyField(const BookOverride& o) {
  return o.title[0] || o.author[0] || o.series[0] || o.seriesIndex[0] || o.genre[0];
}

bool parseHexName(const char* name, uint64_t& hash) {
  if (strlen(name) != HASH_NAME_LEN + 5 || strcmp(name + HASH_NAME_LEN, ".json") != 0) return false;
  uint64_t v = 0;
  for (size_t i = 0; i < HASH_NAME_LEN; i++) {
    const char c = name[i];
    uint8_t d;
    if (c >= '0' && c <= '9') {
      d = static_cast<uint8_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      d = static_cast<uint8_t>(c - 'a' + 10);
    } else {
      return false;
    }
    v = (v << 4) | d;
  }
  hash = v;
  return true;
}

// Hashes of override files whose stored "path" lies under dir. Yields between entries.
std::vector<uint64_t> collectUnder(const char* dir) {
  std::vector<uint64_t> found;
  std::string prefix(dir);
  while (prefix.size() > 1 && prefix.back() == '/') prefix.pop_back();
  prefix.push_back('/');

  HalFile root = Storage.open(META_DIR);
  if (!root || !root.isDirectory()) return found;

  JsonDocument doc;
  char name[64];
  for (HalFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    vTaskDelay(1);
    uint64_t hash;
    if (entry.isDirectory()) continue;
    entry.getName(name, sizeof(name));
    entry.close();
    if (!parseHexName(name, hash)) continue;
    char path[48];
    hashToPath(hash, path);
    doc.clear();
    if (!PersistableStoreBase::readDocFromFile(path, doc)) continue;
    const char* bookPath = doc["path"] | "";
    if (strncmp(bookPath, prefix.c_str(), prefix.size()) == 0) found.push_back(hash);
  }
  return found;
}

}  // namespace

bool load(const char* bookPath, BookOverride& out) {
  memset(&out, 0, sizeof(out));
  char path[48];
  hashToPath(pathHash(bookPath), path);
  if (!Storage.exists(path)) return false;
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(path, doc)) return false;
  copyField(out.title, sizeof(out.title), doc["title"] | "");
  copyField(out.author, sizeof(out.author), doc["author"] | "");
  copyField(out.series, sizeof(out.series), doc["series"] | "");
  copyField(out.seriesIndex, sizeof(out.seriesIndex), doc["seriesIndex"] | "");
  copyField(out.genre, sizeof(out.genre), doc["genre"] | "");
  return true;
}

bool save(const char* bookPath, const BookOverride& in) {
  if (!hasAnyField(in)) return remove(bookPath);

  if (!Storage.exists(META_DIR) && !Storage.mkdir(META_DIR)) {
    LOG_ERR("BMETA", "cannot create %s", META_DIR);
    return false;
  }
  JsonDocument doc;
  doc["path"] = bookPath;
  setIfNonEmpty(doc, "title", in.title);
  setIfNonEmpty(doc, "author", in.author);
  setIfNonEmpty(doc, "series", in.series);
  setIfNonEmpty(doc, "seriesIndex", in.seriesIndex);
  setIfNonEmpty(doc, "genre", in.genre);

  const uint64_t hash = pathHash(bookPath);
  char path[48];
  hashToPath(hash, path);
  if (!PersistableStoreBase::writeDocToFile(path, doc)) return false;
  noteChanged(&hash, 1);
  return true;
}

bool remove(const char* bookPath) {
  const uint64_t hash = pathHash(bookPath);
  char path[48];
  hashToPath(hash, path);
  if (!Storage.exists(path)) return true;
  if (!Storage.remove(path)) {
    LOG_ERR("BMETA", "cannot remove %s", path);
    return false;
  }
  noteChanged(&hash, 1);
  return true;
}

bool move(const char* oldPath, const char* newPath) {
  const uint64_t oldHash = pathHash(oldPath);
  char from[48];
  hashToPath(oldHash, from);
  if (!Storage.exists(from)) return true;

  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(from, doc)) return false;
  doc["path"] = newPath;
  const uint64_t newHash = pathHash(newPath);
  char to[48];
  hashToPath(newHash, to);
  if (!PersistableStoreBase::writeDocToFile(to, doc)) return false;
  if (newHash != oldHash && !Storage.remove(from)) LOG_ERR("BMETA", "cannot remove %s", from);
  const uint64_t hashes[2] = {oldHash, newHash};
  noteChanged(hashes, 2);
  return true;
}

void moveTree(const char* oldDir, const char* newDir) {
  std::string from(oldDir);
  std::string to(newDir);
  while (from.size() > 1 && from.back() == '/') from.pop_back();
  while (to.size() > 1 && to.back() == '/') to.pop_back();

  const std::vector<uint64_t> found = collectUnder(from.c_str());
  std::vector<uint64_t> changed;
  changed.reserve(found.size() * 2);
  JsonDocument doc;
  for (const uint64_t oldHash : found) {
    vTaskDelay(1);
    char path[48];
    hashToPath(oldHash, path);
    doc.clear();
    if (!PersistableStoreBase::readDocFromFile(path, doc)) continue;
    const std::string newBook = to + ((doc["path"] | "") + from.size());
    doc["path"] = newBook.c_str();
    const uint64_t newHash = pathHash(newBook.c_str());
    char newFile[48];
    hashToPath(newHash, newFile);
    if (!PersistableStoreBase::writeDocToFile(newFile, doc)) continue;
    if (newHash != oldHash && !Storage.remove(path)) LOG_ERR("BMETA", "cannot remove %s", path);
    changed.push_back(oldHash);
    changed.push_back(newHash);
  }
  if (!changed.empty()) noteChanged(changed.data(), changed.size());
}

void removeTree(const char* dir) {
  const std::vector<uint64_t> found = collectUnder(dir);
  for (const uint64_t hash : found) {
    char path[48];
    hashToPath(hash, path);
    if (!Storage.remove(path)) LOG_ERR("BMETA", "cannot remove %s", path);
    vTaskDelay(1);
  }
  if (!found.empty()) noteChanged(found.data(), found.size());
}

}  // namespace bookmeta
