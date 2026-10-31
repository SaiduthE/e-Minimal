#pragma once

#include <cstddef>
#include <string>
#include <vector>

// One StarDict dictionary found under /dictionaries or /.dictionaries: a
// subfolder holding <stem>.idx plus <stem>.dict or <stem>.dict.dz.
struct DictionaryEntry {
  std::string name;  // subfolder name (shown to the user, stored in settings)
  std::string stem;  // index basename without .idx
  const char* root = "";  // "/dictionaries" or "/.dictionaries"
  bool hasSyn = false;
  bool compressed = false;  // .dict.dz rather than .dict
};

namespace DictionaryRegistry {

// Scan /dictionaries/*/ and /.dictionaries/*/ for dictionaries. Folders with
// multiple index stems are ambiguous and skipped. Result is sorted
// case-insensitively by name.
void discover(std::vector<DictionaryEntry>& out);

// Resolve a folder name to its extensionless base path
// ("/dictionaries/<folder>/<stem>" or "/.dictionaries/<folder>/<stem>").
// Returns false if the folder holds no usable dictionary in either root.
bool resolveBasePath(const char* folderName, std::string& basePathOut);

// True if name is usable as a dictionary folder name: non-empty, no separators,
// no dot prefix. Same rules resolveBasePath applies.
bool isValidFolderName(const char* name);

// Recursively delete the dictionary folder `name` from whichever root holds it,
// or only from `onlyRoot` ("/dictionaries" or "/.dictionaries") when given.
// Returns false for an invalid name or root, or when nothing was deleted.
bool deleteDictionary(const char* name, const char* onlyRoot = nullptr);

enum class RenameResult {
  Ok,
  InvalidName,  // empty, separator, dot prefix, FAT-reserved character, or longer than maxLen
  NameTaken,    // another folder with that name exists in either root
  Failed,       // the card refused the rename
};

// Rename the dictionary folder `oldName` inside `root` to `newName`; the folder
// stays in its own root. A case-only change of the same folder is allowed.
// `maxLen` is the longest name the caller can store (the settings buffer less its NUL).
RenameResult renameDictionary(const char* oldName, const char* newName, const char* root, size_t maxLen);

enum class SelectionState {
  Ready,       // the stored name resolves
  NoneFound,   // no dictionary on the card (name cleared)
  AutoPicked,  // exactly one dictionary: name was set to it; caller must persist
  NeedChoice,  // several dictionaries: name cleared, `list` holds the choices
};

// Make `name` (a settings buffer of `capacity` bytes) point at a usable
// dictionary. A stale or empty name is cleared first. `list` is filled only for
// NoneFound/AutoPicked/NeedChoice. Entries whose name would not fit `capacity`
// are left out. `changed` (optional) is set when `name` was modified (a stale name cleared or
// one dictionary auto-picked): the caller persists settings only then.
SelectionState ensureSelection(char* name, size_t capacity, std::vector<DictionaryEntry>& list,
                               bool* changed = nullptr);

}  // namespace DictionaryRegistry
