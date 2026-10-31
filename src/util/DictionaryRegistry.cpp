#include "DictionaryRegistry.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "StringUtils.h"

namespace DictionaryRegistry {
namespace {

// Dictionaries are looked up in both roots, in order. The hidden variant
// lets users keep the folder out of the file browser (hidden by default,
// see FileBrowserActivity's showHiddenFiles check).
constexpr const char* DICT_ROOTS[] = {"/dictionaries", "/.dictionaries"};

// Find the single .idx stem inside one dictionary folder. Returns false when
// the folder holds no .idx or more than one distinct stem (ambiguous).
bool findStem(const char* folderPath, std::string& stemOut) {
  auto dir = Storage.open(folderPath);
  if (!dir || !dir.isDirectory()) return false;

  dir.rewindDirectory();
  char name[128];
  char foundStem[128];
  foundStem[0] = '\0';
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    entry.getName(name, sizeof(name));
    // Skip macOS metadata files (AppleDouble resource forks)
    if (entry.isDirectory() || strncmp(name, "._", 2) == 0) continue;

    const size_t len = strlen(name);
    if (len <= 4 || strcmp(name + len - 4, ".idx") != 0) continue;

    name[len - 4] = '\0';
    if (foundStem[0] != '\0' && strcmp(foundStem, name) != 0) {
      LOG_DBG("DREG", "Skipping %s: multiple index stems found", folderPath);
      return false;
    }
    strncpy(foundStem, name, sizeof(foundStem) - 1);
    foundStem[sizeof(foundStem) - 1] = '\0';
  }

  if (foundStem[0] == '\0') return false;

  // Require dictionary data next to the index, so folders holding only an
  // .idx never surface as selectable dictionaries that fail at lookup time.
  const std::string base = std::string(folderPath) + "/" + foundStem;
  if (!Storage.exists((base + ".dict").c_str()) && !Storage.exists((base + ".dict.dz").c_str())) {
    LOG_DBG("DREG", "Skipping %s: no .dict or .dict.dz", folderPath);
    return false;
  }

  stemOut = foundStem;
  return true;
}

}  // namespace

void discover(std::vector<DictionaryEntry>& out) {
  out.clear();
  out.reserve(8);

  for (const char* dictRoot : DICT_ROOTS) {
    auto rootDir = Storage.open(dictRoot);
    if (!rootDir || !rootDir.isDirectory()) {
      LOG_DBG("DREG", "No %s directory on SD card", dictRoot);
      continue;
    }

    rootDir.rewindDirectory();
    char name[128];
    for (auto entry = rootDir.openNextFile(); entry; entry = rootDir.openNextFile()) {
      entry.getName(name, sizeof(name));
      if (!entry.isDirectory() || name[0] == '.') continue;

      std::string folderPath = std::string(dictRoot) + "/" + name;
      std::string stem;
      if (!findStem(folderPath.c_str(), stem)) continue;

      DictionaryEntry e;
      e.name = name;
      e.stem = std::move(stem);
      e.root = dictRoot;
      const std::string base = folderPath + "/" + e.stem;
      e.hasSyn = Storage.exists((base + ".syn").c_str());
      e.compressed = !Storage.exists((base + ".dict").c_str());
      out.push_back(std::move(e));
      LOG_DBG("DREG", "Found dictionary: %s", name);
    }
  }

  // Case-insensitive sort by folder name (matches FileBrowserActivity ordering).
  std::sort(out.begin(), out.end(), [](const DictionaryEntry& a, const DictionaryEntry& b) {
    return StringUtils::asciiCaseCmp(a.name.c_str(), b.name.c_str()) < 0;
  });
}

bool isValidFolderName(const char* name) {
  // The name is persisted in the settings JSON: reject separators and dot
  // prefixes so a crafted value cannot escape the dictionary roots.
  return name && name[0] != '\0' && name[0] != '.' && strpbrk(name, "/\\") == nullptr;
}

bool deleteDictionary(const char* name, const char* onlyRoot) {
  if (!isValidFolderName(name)) return false;
  if (onlyRoot) {
    bool known = false;
    for (const char* dictRoot : DICT_ROOTS) known = known || strcmp(dictRoot, onlyRoot) == 0;
    if (!known) return false;
  }
  bool deleted = false;
  for (const char* dictRoot : DICT_ROOTS) {
    if (onlyRoot && strcmp(dictRoot, onlyRoot) != 0) continue;
    const std::string folderPath = std::string(dictRoot) + "/" + name;
    if (!Storage.exists(folderPath.c_str())) continue;
    if (Storage.removeDir(folderPath.c_str())) {
      deleted = true;
    } else {
      LOG_ERR("DREG", "Failed to delete %s", folderPath.c_str());
    }
  }
  return deleted;
}

RenameResult renameDictionary(const char* oldName, const char* newName, const char* root, const size_t maxLen) {
  if (!isValidFolderName(oldName) || !isValidFolderName(newName)) return RenameResult::InvalidName;
  const size_t newLen = strlen(newName);
  // SdFat strips trailing dots and spaces from a path component, so such a name
  // would be stored differently from the folder it creates.
  if (newLen > maxLen || strpbrk(newName, ":*?\"<>|") != nullptr || newName[0] == ' ' ||
      newName[newLen - 1] == ' ' || newName[newLen - 1] == '.') {
    return RenameResult::InvalidName;
  }
  for (size_t i = 0; i < newLen; i++) {
    if (static_cast<unsigned char>(newName[i]) < 0x20) return RenameResult::InvalidName;
  }
  bool knownRoot = false;
  for (const char* dictRoot : DICT_ROOTS) knownRoot = knownRoot || (root && strcmp(dictRoot, root) == 0);
  if (!knownRoot) return RenameResult::InvalidName;

  const bool sameFolder = StringUtils::asciiCaseCmp(oldName, newName) == 0;
  if (sameFolder && strcmp(oldName, newName) == 0) return RenameResult::Ok;

  // FAT names compare case-insensitively, so the old folder would collide with
  // its own new spelling; only the other root needs checking then.
  for (const char* dictRoot : DICT_ROOTS) {
    if (sameFolder && strcmp(dictRoot, root) == 0) continue;
    const std::string taken = std::string(dictRoot) + "/" + newName;
    if (Storage.exists(taken.c_str())) return RenameResult::NameTaken;
  }

  const std::string from = std::string(root) + "/" + oldName;
  const std::string to = std::string(root) + "/" + newName;
  if (sameFolder) {
    // A case-only change can't rename in place: SdFat creates the new entry with
    // O_EXCL and the case-insensitive lookup finds the old one. Go via a free
    // temporary name, putting the folder back if the second step fails.
    std::string temp = from + "~ren";
    for (int i = 0; Storage.exists(temp.c_str()) && i < 10; i++) temp = from + "~ren" + std::to_string(i);
    if (Storage.exists(temp.c_str()) || !Storage.rename(from.c_str(), temp.c_str())) {
      LOG_ERR("DREG", "Failed to rename %s to a temporary name", from.c_str());
      return RenameResult::Failed;
    }
    if (!Storage.rename(temp.c_str(), to.c_str())) {
      LOG_ERR("DREG", "Failed to rename %s to %s", temp.c_str(), to.c_str());
      if (!Storage.rename(temp.c_str(), from.c_str())) LOG_ERR("DREG", "Could not restore %s", from.c_str());
      return RenameResult::Failed;
    }
    return RenameResult::Ok;
  }
  if (!Storage.rename(from.c_str(), to.c_str())) {
    LOG_ERR("DREG", "Failed to rename %s to %s", from.c_str(), to.c_str());
    return RenameResult::Failed;
  }
  return RenameResult::Ok;
}

SelectionState ensureSelection(char* name, size_t capacity, std::vector<DictionaryEntry>& list, bool* changed) {
  if (changed) *changed = false;
  std::string base;
  if (name[0] != '\0') {
    if (resolveBasePath(name, base)) return SelectionState::Ready;
    name[0] = '\0';  // folder deleted or renamed
    if (changed) *changed = true;
  }

  discover(list);
  list.erase(std::remove_if(list.begin(), list.end(),
                            [capacity](const DictionaryEntry& e) { return e.name.size() >= capacity; }),
             list.end());
  if (list.empty()) return SelectionState::NoneFound;
  if (list.size() == 1) {
    strncpy(name, list[0].name.c_str(), capacity - 1);
    name[capacity - 1] = '\0';
    if (changed) *changed = true;
    return SelectionState::AutoPicked;
  }
  return SelectionState::NeedChoice;
}

bool resolveBasePath(const char* folderName, std::string& basePathOut) {
  if (!isValidFolderName(folderName)) return false;

  for (const char* dictRoot : DICT_ROOTS) {
    std::string folderPath = std::string(dictRoot) + "/" + folderName;
    std::string stem;
    if (!findStem(folderPath.c_str(), stem)) continue;
    basePathOut = folderPath + "/" + stem;
    return true;
  }
  return false;
}

}  // namespace DictionaryRegistry
