#include "TodoStore.h"

#include <Logging.h>

#include <algorithm>

void TodoStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["items"].to<JsonArray>();
  for (const auto& item : items) {
    JsonObject obj = arr.add<JsonObject>();
    obj["text"] = item.text;
    obj["done"] = item.done;
  }
}

bool TodoStore::fromJson(JsonVariantConst doc) {
  items.clear();
  JsonArrayConst arr = doc["items"].as<JsonArrayConst>();
  for (JsonObjectConst obj : arr) {
    if (items.size() >= MAX_ITEMS) break;
    const char* text = obj["text"] | "";
    if (text[0] == '\0') continue;
    TodoItem item;
    item.text = text;
    if (item.text.size() > MAX_TEXT_LENGTH) item.text.resize(MAX_TEXT_LENGTH);
    item.done = obj["done"] | false;
    items.push_back(std::move(item));
  }
  LOG_DBG("TODO", "Loaded %zu to-do items", items.size());
  return true;
}

void TodoStore::load() {
  items.clear();
  items.reserve(MAX_ITEMS);
  if (!loadFromFile()) items.clear();
}

void TodoStore::unload() {
  items.clear();
  items.shrink_to_fit();
}

bool TodoStore::add(const std::string& text) {
  if (text.empty() || isFull()) return false;
  TodoItem item;
  item.text = text.substr(0, MAX_TEXT_LENGTH);
  items.push_back(std::move(item));
  return true;
}

bool TodoStore::setText(const size_t index, const std::string& text) {
  if (index >= items.size() || text.empty()) return false;
  items[index].text = text.substr(0, MAX_TEXT_LENGTH);
  return true;
}

bool TodoStore::toggle(const size_t index) {
  if (index >= items.size()) return false;
  items[index].done = !items[index].done;
  return true;
}

bool TodoStore::remove(const size_t index) {
  if (index >= items.size()) return false;
  items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
  return true;
}

bool TodoStore::removeDone() {
  const size_t before = items.size();
  items.erase(std::remove_if(items.begin(), items.end(), [](const TodoItem& item) { return item.done; }), items.end());
  return items.size() != before;
}

size_t TodoStore::doneCount() const {
  return static_cast<size_t>(std::count_if(items.begin(), items.end(), [](const TodoItem& item) { return item.done; }));
}
