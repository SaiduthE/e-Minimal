#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstddef>
#include <string>
#include <vector>

struct TodoItem {
  std::string text;
  bool done = false;
};

// The dashboard's to-do list, on SD at /.crosspoint/todo.json. Only the
// dashboard uses it: it loads on the dashboard's onEnter() and unload() hands
// the heap back on onExit(), so the list costs nothing while reading.
class TodoStore : public PersistableStore<TodoStore> {
  std::vector<TodoItem> items;

  TodoStore() = default;

  friend class PersistableStore<TodoStore>;

 public:
  static constexpr size_t MAX_ITEMS = 50;
  static constexpr size_t MAX_TEXT_LENGTH = 80;

  static const char* getFilePath() { return "/.crosspoint/todo.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Reads the file into a list reserved to MAX_ITEMS (~1.4 KB), so adds never
  // regrow it. A missing or unreadable file leaves the list empty.
  void load();
  void unload();

  // Mutators change memory only (false when the index or capacity is bad);
  // the caller saves with saveToFile(), outside any render lock.
  bool add(const std::string& text);
  bool setText(size_t index, const std::string& text);
  bool toggle(size_t index);
  bool remove(size_t index);
  // Removes every done item. False when none were done.
  bool removeDone();

  const std::vector<TodoItem>& getItems() const { return items; }
  size_t count() const { return items.size(); }
  size_t doneCount() const;
  bool isFull() const { return items.size() >= MAX_ITEMS; }
};

#define TODO_STORE TodoStore::getInstance()
