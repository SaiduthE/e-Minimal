#include "DashboardConfigStore.h"

#include <I18n.h>
#include <Logging.h>
#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr const char* WIDGET_KEYS[] = {"none", "weather", "todo", "image", "date"};

// The starting layouts, in order: their names and the widget in each tile.
constexpr StrId DEFAULT_NAMES[DashboardConfigStore::DEFAULT_LAYOUT_COUNT] = {
    StrId::STR_DASH_LAYOUT_WIDE_TOP, StrId::STR_DASH_LAYOUT_GRID, StrId::STR_DASH_LAYOUT_PICTURE_TOP};
constexpr WidgetId DEFAULT_WIDGETS[DashboardConfigStore::DEFAULT_LAYOUT_COUNT][4] = {
    {WidgetId::WEATHER, WidgetId::TODO, WidgetId::IMAGE, WidgetId::NONE},
    {WidgetId::WEATHER, WidgetId::TODO, WidgetId::IMAGE, WidgetId::DATE},
    {WidgetId::IMAGE, WidgetId::WEATHER, WidgetId::TODO, WidgetId::NONE},
};

// Tiles in reading order: top to bottom, then left to right (the focus order).
void sortReadingOrder(DashboardTile* tiles, const uint8_t count) {
  std::sort(tiles, tiles + count, [](const DashboardTile& a, const DashboardTile& b) {
    return a.y != b.y ? a.y < b.y : a.x < b.x;
  });
}

DashboardTile makeTile(const WidgetId widget, const uint8_t x, const uint8_t y, const uint8_t w, const uint8_t h) {
  DashboardTile tile;
  tile.widget = widget;
  tile.x = x;
  tile.y = y;
  tile.width = w;
  tile.height = h;
  return tile;
}

// Starting layout `index` on the grid of that shape: 0 Wide top (40% on top,
// two below), 1 Grid (2x2), 2 Picture top (60% on top, two below).
CustomLayout defaultLayout(const uint8_t index, const bool portrait) {
  CustomLayout layout;
  layout.name = I18N.get(DEFAULT_NAMES[index]);
  layout.portrait = portrait;
  const uint8_t cols = dashboard_layout::columns(portrait);
  const uint8_t rowCount = dashboard_layout::rows(portrait);
  const uint8_t leftWidth = cols / 2;
  const uint8_t rightWidth = cols - leftWidth;
  const WidgetId* widgets = DEFAULT_WIDGETS[index];
  if (index == 1) {
    const uint8_t topHeight = rowCount / 2;
    layout.tiles[0] = makeTile(widgets[0], 0, 0, leftWidth, topHeight);
    layout.tiles[1] = makeTile(widgets[1], leftWidth, 0, rightWidth, topHeight);
    layout.tiles[2] = makeTile(widgets[2], 0, topHeight, leftWidth, rowCount - topHeight);
    layout.tiles[3] = makeTile(widgets[3], leftWidth, topHeight, rightWidth, rowCount - topHeight);
    layout.tileCount = 4;
    return layout;
  }
  const uint8_t topHeight = static_cast<uint8_t>(std::lround(rowCount * (index == 2 ? 0.6f : 0.4f)));
  layout.tiles[0] = makeTile(widgets[0], 0, 0, cols, topHeight);
  layout.tiles[1] = makeTile(widgets[1], 0, topHeight, leftWidth, rowCount - topHeight);
  layout.tiles[2] = makeTile(widgets[2], leftWidth, topHeight, rightWidth, rowCount - topHeight);
  layout.tileCount = 3;
  return layout;
}
}  // namespace

const char* DashboardConfigStore::widgetKey(const WidgetId widget) {
  const auto index = static_cast<size_t>(widget);
  return index < sizeof(WIDGET_KEYS) / sizeof(WIDGET_KEYS[0]) ? WIDGET_KEYS[index] : nullptr;
}

bool DashboardConfigStore::widgetFromKey(const char* key, WidgetId& out) {
  if (!key) return false;
  for (size_t i = 0; i < sizeof(WIDGET_KEYS) / sizeof(WIDGET_KEYS[0]); i++) {
    if (strcmp(key, WIDGET_KEYS[i]) == 0) {
      out = static_cast<WidgetId>(i);
      return true;
    }
  }
  return false;
}

const char* CustomLayout::validate() const {
  if (name.empty() || name.size() > MAX_NAME_LENGTH) return "name";
  if (tileCount == 0 || tileCount > MAX_TILES) return "tiles";
  static const char* const PATHS[MAX_TILES] = {"tiles[0]", "tiles[1]", "tiles[2]", "tiles[3]",
                                               "tiles[4]", "tiles[5]", "tiles[6]", "tiles[7]"};
  for (uint8_t i = 0; i < tileCount; i++) {
    const DashboardTile& tile = tiles[i];
    const bool placed = tile.widget != WidgetId::NONE && DashboardConfigStore::widgetKey(tile.widget) != nullptr;
    const bool fits = tile.width >= dashboard_layout::MIN_TILE_CELLS &&
                      tile.height >= dashboard_layout::MIN_TILE_CELLS && tile.x + tile.width <= columns() &&
                      tile.y + tile.height <= rows();
    if (!placed || !fits) return PATHS[i];
    for (uint8_t j = 0; j < i; j++) {
      const DashboardTile& other = tiles[j];
      const bool overlaps = tile.x < other.x + other.width && other.x < tile.x + tile.width &&
                            tile.y < other.y + other.height && other.y < tile.y + tile.height;
      if (overlaps) return PATHS[i];
    }
  }
  return nullptr;
}

void CustomLayout::toJson(JsonObject out) const {
  out["id"] = id;
  out["name"] = name;
  out["shape"] = portrait ? "portrait" : "landscape";
  JsonArray list = out["tiles"].to<JsonArray>();
  for (uint8_t i = 0; i < tileCount; i++) {
    JsonObject tile = list.add<JsonObject>();
    tile["widget"] = DashboardConfigStore::widgetKey(tiles[i].widget);
    tile["x"] = tiles[i].x;
    tile["y"] = tiles[i].y;
    tile["w"] = tiles[i].width;
    tile["h"] = tiles[i].height;
  }
}

bool CustomLayout::fromJson(JsonObjectConst in) {
  if (!in["name"].is<const char*>() || !in["tiles"].is<JsonArrayConst>()) return false;
  name = in["name"].as<const char*>();
  const char* shape = in["shape"] | "landscape";
  if (strcmp(shape, "portrait") == 0) {
    portrait = true;
  } else if (strcmp(shape, "landscape") == 0) {
    portrait = false;
  } else {
    return false;
  }
  tileCount = 0;
  for (JsonObjectConst tile : in["tiles"].as<JsonArrayConst>()) {
    if (tileCount >= MAX_TILES) return false;
    DashboardTile& out = tiles[tileCount++];
    if (!DashboardConfigStore::widgetFromKey(tile["widget"] | "", out.widget)) return false;
    for (const char* key : {"x", "y", "w", "h"}) {
      if (!tile[key].is<int>() || tile[key].as<int>() < 0 || tile[key].as<int>() > 255) return false;
    }
    out.x = tile["x"].as<uint8_t>();
    out.y = tile["y"].as<uint8_t>();
    out.width = tile["w"].as<uint8_t>();
    out.height = tile["h"].as<uint8_t>();
  }
  return true;
}

const CustomLayout* DashboardConfigStore::findLayout(const uint8_t id) const {
  if (id == 0) return nullptr;
  for (const auto& layout : layouts) {
    if (layout.id == id) return &layout;
  }
  return nullptr;
}

bool DashboardConfigStore::activateLayout(const uint8_t id) {
  if (!findLayout(id)) return false;
  activeLayoutId = id;
  return true;
}

bool DashboardConfigStore::saveLayout(CustomLayout& layout) {
  if (layout.name.size() > CustomLayout::MAX_NAME_LENGTH) {
    layout.name.resize(utf8SafeTruncateBuffer(layout.name.c_str(), static_cast<int>(CustomLayout::MAX_NAME_LENGTH)));
  }
  if (layout.validate() != nullptr) return false;
  for (auto& existing : layouts) {
    if (layout.id != 0 && existing.id == layout.id) {
      existing = layout;
      return true;
    }
  }
  if (layouts.size() >= MAX_LAYOUTS) return false;
  uint8_t id = 1;
  while (findLayout(id) && id < 255) id++;
  if (findLayout(id)) return false;
  layout.id = id;
  layouts.push_back(layout);
  return true;
}

bool DashboardConfigStore::deleteLayout(const uint8_t id) {
  for (auto it = layouts.begin(); it != layouts.end(); ++it) {
    if (it->id != id) continue;
    layouts.erase(it);
    if (activeLayoutId == id) activeLayoutId = layouts.empty() ? 0 : layouts.front().id;
    return true;
  }
  return false;
}

uint8_t DashboardConfigStore::restoreDefaultLayouts(const bool portrait) {
  uint8_t added = 0;
  for (uint8_t index = 0; index < DEFAULT_LAYOUT_COUNT; index++) {
    CustomLayout layout = defaultLayout(index, portrait);
    const bool present = std::any_of(layouts.begin(), layouts.end(),
                                     [&layout](const CustomLayout& existing) { return existing.name == layout.name; });
    if (present) continue;
    if (!saveLayout(layout)) break;  // full
    added++;
  }
  if (!activeLayout() && !layouts.empty()) activeLayoutId = layouts.front().id;
  return added;
}

uint8_t DashboardConfigStore::layoutTiles(const uint8_t id, const bool portrait, DashboardTile* out) const {
  const CustomLayout* layout = findLayout(id);
  if (!layout) return 0;
  const uint8_t count = layout->tileCount;
  for (uint8_t i = 0; i < count; i++) {
    out[i] = layout->tiles[i];
    if (layout->portrait != portrait) {
      // Drawn for the other shape: turn it on its side (the cells are square).
      std::swap(out[i].x, out[i].y);
      std::swap(out[i].width, out[i].height);
    }
  }
  sortReadingOrder(out, count);
  return count;
}

bool DashboardConfigStore::activeLayoutShows(const WidgetId widget) const {
  const CustomLayout* layout = activeLayout();
  if (!layout) return false;
  for (uint8_t i = 0; i < layout->tileCount; i++) {
    if (layout->tiles[i].widget == widget) return true;
  }
  return false;
}

void DashboardConfigStore::migratePresets(JsonVariantConst doc) {
  static constexpr const char* PRESET_KEYS[DEFAULT_LAYOUT_COUNT] = {"a", "b", "c"};
  JsonVariantConst oldLayout = doc["layout"];
  layouts.clear();
  activeLayoutId = 0;

  // Presets A/B/C become the starting layouts, keeping their widgets and any
  // tiles reshaped in the editor.
  uint8_t presetIds[DEFAULT_LAYOUT_COUNT] = {};
  const char* activePreset = oldLayout["preset"] | "a";
  for (uint8_t p = 0; p < DEFAULT_LAYOUT_COUNT; p++) {
    CustomLayout layout = defaultLayout(p, false);
    JsonVariantConst edited = doc["presetLayouts"][PRESET_KEYS[p]];
    CustomLayout reshaped;
    if (edited.is<JsonObjectConst>() && reshaped.fromJson(edited.as<JsonObjectConst>())) {
      reshaped.name = layout.name;
      if (reshaped.validate() == nullptr) layout = reshaped;
    } else {
      // Per-preset widgets, or the single list the earliest files kept for the preset in use.
      JsonVariantConst slots = doc["presets"][PRESET_KEYS[p]];
      if (!slots.is<JsonArrayConst>() && strcmp(activePreset, PRESET_KEYS[p]) == 0) slots = oldLayout["slots"];
      uint8_t slot = 0;
      for (JsonVariantConst key : slots.as<JsonArrayConst>()) {
        WidgetId widget;
        if (slot >= layout.tileCount) break;
        if (widgetFromKey(key | "", widget)) layout.tiles[slot].widget = widget;
        slot++;
      }
      // A slot set to Empty leaves a gap, not a tile.
      uint8_t kept = 0;
      for (uint8_t i = 0; i < layout.tileCount; i++) {
        if (layout.tiles[i].widget != WidgetId::NONE) layout.tiles[kept++] = layout.tiles[i];
      }
      layout.tileCount = kept;
    }
    if (layout.tileCount > 0 && saveLayout(layout)) presetIds[p] = layout.id;
  }

  // Then the custom layouts, renumbered after them.
  const int oldActiveCustom = oldLayout["custom"] | 0;
  for (JsonObjectConst entry : doc["customLayouts"].as<JsonArrayConst>()) {
    CustomLayout layout;
    if (!layout.fromJson(entry) || layout.validate() != nullptr) continue;
    const int oldId = entry["id"] | 0;
    layout.id = 0;
    if (!saveLayout(layout)) break;
    if (oldId != 0 && oldId == oldActiveCustom) activeLayoutId = layout.id;
  }

  if (activeLayoutId == 0) {
    for (uint8_t p = 0; p < DEFAULT_LAYOUT_COUNT; p++) {
      if (strcmp(activePreset, PRESET_KEYS[p]) == 0) activeLayoutId = presetIds[p];
    }
  }
  if (!activeLayout()) activeLayoutId = layouts.empty() ? 0 : layouts.front().id;
  LOG_INF("DASHCFG", "Moved presets and %u layouts to the layout list", static_cast<unsigned>(layouts.size()));
  requestResave();
}

void DashboardConfigStore::toJson(JsonDocument& doc) const {
  doc["layout"].to<JsonObject>()["active"] = activeLayoutId;
  JsonArray list = doc["layouts"].to<JsonArray>();
  for (const auto& layout : layouts) layout.toJson(list.add<JsonObject>());

  JsonObject weather = doc["weather"].to<JsonObject>();
  if (weatherHasLocation) {
    weather["place"] = weatherPlace;
    weather["lat"] = weatherLatitude;
    weather["lon"] = weatherLongitude;
  }
  weather["units"] = weatherFahrenheit ? "f" : "c";

  JsonObject carousel = doc["carousel"].to<JsonObject>();
  carousel["folder"] = carouselFolder;
  carousel["intervalMinutes"] = carouselIntervalMinutes;
}

bool DashboardConfigStore::fromJson(JsonVariantConst doc) {
  if (doc["layouts"].is<JsonArrayConst>()) {
    layouts.clear();
    layouts.reserve(MAX_LAYOUTS);
    for (JsonObjectConst entry : doc["layouts"].as<JsonArrayConst>()) {
      if (layouts.size() >= MAX_LAYOUTS) break;
      CustomLayout layout;
      const int id = entry["id"] | 0;
      // A layout that no longer validates (a hand-edited file) is dropped, not half-drawn.
      if (id <= 0 || id > 255 || findLayout(static_cast<uint8_t>(id)) || !layout.fromJson(entry) ||
          layout.validate() != nullptr) {
        LOG_ERR("DASHCFG", "Dropped layout %d", id);
        requestResave();
        continue;
      }
      layout.id = static_cast<uint8_t>(id);
      layouts.push_back(std::move(layout));
    }
    const int active = doc["layout"]["active"] | 0;
    activeLayoutId = active > 0 && active <= 255 && findLayout(static_cast<uint8_t>(active))
                         ? static_cast<uint8_t>(active)
                         : (layouts.empty() ? 0 : layouts.front().id);
  } else if (doc["customLayouts"].is<JsonArrayConst>() || doc["presets"].is<JsonObjectConst>() ||
             doc["layout"]["preset"].is<const char*>()) {
    migratePresets(doc);
  } else {
    layouts.clear();
    activeLayoutId = 0;
    restoreDefaultLayouts();
  }

  JsonVariantConst weather = doc["weather"];
  clearWeatherLocation();
  if (weather["lat"].is<float>() && weather["lon"].is<float>()) {
    setWeatherLocation(weather["place"] | "", weather["lat"].as<float>(), weather["lon"].as<float>());
  }
  weatherFahrenheit = strcmp(weather["units"] | "c", "f") == 0;

  JsonVariantConst carousel = doc["carousel"];
  if (!setCarouselFolder(carousel["folder"] | DEFAULT_CAROUSEL_FOLDER)) carouselFolder = DEFAULT_CAROUSEL_FOLDER;
  if (!setCarouselIntervalMinutes(carousel["intervalMinutes"] | static_cast<int>(DEFAULT_CAROUSEL_MINUTES))) {
    carouselIntervalMinutes = DEFAULT_CAROUSEL_MINUTES;
  }
  return true;
}

bool DashboardConfigStore::setWeatherLocation(const char* place, const float latitude, const float longitude) {
  if (!std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90.0f || latitude > 90.0f ||
      longitude < -180.0f || longitude > 180.0f) {
    LOG_ERR("DASHCFG", "Rejected weather location %f,%f", latitude, longitude);
    return false;
  }
  weatherPlace = place ? place : "";
  if (weatherPlace.size() > MAX_PLACE_LENGTH) {
    weatherPlace.resize(utf8SafeTruncateBuffer(weatherPlace.c_str(), static_cast<int>(MAX_PLACE_LENGTH)));
  }
  weatherLatitude = latitude;
  weatherLongitude = longitude;
  weatherHasLocation = true;
  return true;
}

void DashboardConfigStore::clearWeatherLocation() {
  weatherPlace.clear();
  weatherLatitude = 0.0f;
  weatherLongitude = 0.0f;
  weatherHasLocation = false;
}

bool DashboardConfigStore::setCarouselFolder(const char* folder) {
  // An absolute SD path with no parent hops; the trailing slash is dropped so
  // "/carousel" and "/carousel/" name the same folder.
  if (!folder || folder[0] != '/' || strstr(folder, "..") != nullptr) return false;
  std::string value = folder;
  while (value.size() > 1 && value.back() == '/') value.pop_back();
  if (value.size() > MAX_FOLDER_LENGTH) return false;
  carouselFolder = std::move(value);
  return true;
}

bool DashboardConfigStore::setCarouselIntervalMinutes(const int minutes) {
  if (minutes < MIN_CAROUSEL_MINUTES || minutes > MAX_CAROUSEL_MINUTES) return false;
  carouselIntervalMinutes = static_cast<uint16_t>(minutes);
  return true;
}
