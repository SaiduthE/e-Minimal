#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// A tile's content on the tiled dashboard. Persisted by key (widgetKey()).
enum class WidgetId : uint8_t { NONE = 0, WEATHER, TODO, IMAGE, DATE };

// One widget on the dashboard grid, in cells.
struct DashboardTile {
  WidgetId widget = WidgetId::NONE;
  uint8_t x = 0;
  uint8_t y = 0;
  uint8_t width = 0;
  uint8_t height = 0;
};

// The dashboard grid: GRID_LONG x GRID_SHORT cells of CELL_PX on a landscape
// screen, GRID_SHORT x GRID_LONG in portrait. CELL_PX is a whole number of
// the panel's partial-refresh blocks (32-px columns, 16-row bands of panel
// memory) along either axis, and the grid is placed so every inner tile edge
// lands on a block boundary in each orientation (dashboard_grid): a tile can
// refresh on its own.
namespace dashboard_layout {
constexpr uint8_t GRID_LONG = 14;
constexpr uint8_t GRID_SHORT = 10;
constexpr uint16_t CELL_PX = 128;
constexpr uint8_t MIN_TILE_CELLS = 2;  // per side

inline uint8_t columns(const bool portrait) { return portrait ? GRID_SHORT : GRID_LONG; }
inline uint8_t rows(const bool portrait) { return portrait ? GRID_LONG : GRID_SHORT; }
}  // namespace dashboard_layout

// A dashboard layout: named tiles on the grid of its shape, each spanning whole
// cells. Every layout is one of these -- the three the app starts with (Wide
// top, Grid, Picture top) are ordinary layouts, edited, renamed and deleted
// like any other in the web interface. Shown on a dashboard of the other shape
// a layout is turned on its side (x and y swapped).
struct CustomLayout {
  static constexpr size_t MAX_NAME_LENGTH = 32;
  static constexpr uint8_t MAX_TILES = 8;

  uint8_t id = 0;  // 1..255 once saved; 0 for a new one
  std::string name;
  bool portrait = false;
  uint8_t tileCount = 0;
  DashboardTile tiles[MAX_TILES];

  uint8_t columns() const { return dashboard_layout::columns(portrait); }
  uint8_t rows() const { return dashboard_layout::rows(portrait); }
  // nullptr when usable; else the first problem, as a JSON path for the web
  // pages ("name", "tiles", "tiles[2]").
  const char* validate() const;
  // Keys: id, name, shape ("landscape" / "portrait"), tiles [{widget, x, y, w, h}].
  void toJson(JsonObject out) const;
  // Reads name, shape and tiles (not the id); false on a missing or mistyped field.
  bool fromJson(JsonObjectConst in);
};

// Settings for the Dashboard app, on SD at /.crosspoint/dashboards.json: the
// layouts, the one in use, and what the widgets show. The web interface's
// Dashboards page writes it (/api/dashboards); the device reads it with
// loadFromFile(). Without a file the three starting layouts are there. Which
// way the launched dashboard is turned is the device setting
// CrossPointSettings::dashboardOrientation.
class DashboardConfigStore : public PersistableStore<DashboardConfigStore> {
  DashboardConfigStore() { restoreDefaultLayouts(); }

  friend class PersistableStore<DashboardConfigStore>;

 public:
  static constexpr size_t MAX_PLACE_LENGTH = 64;
  static constexpr size_t MAX_FOLDER_LENGTH = 128;
  static constexpr uint16_t MIN_CAROUSEL_MINUTES = 5;
  static constexpr uint16_t MAX_CAROUSEL_MINUTES = 24 * 60;
  static constexpr uint16_t DEFAULT_CAROUSEL_MINUTES = 30;
  static constexpr const char* DEFAULT_CAROUSEL_FOLDER = "/carousel";
  static constexpr uint8_t MAX_LAYOUTS = 12;
  static constexpr uint8_t DEFAULT_LAYOUT_COUNT = 3;

  // All layouts, in the order they were made, and the one in use (0: none,
  // when every layout was deleted).
  std::vector<CustomLayout> layouts;
  uint8_t activeLayoutId = 0;

  // Weather: the name shown in the widget's title and the coordinates
  // Open-Meteo is asked about. No location means the widget asks for one.
  std::string weatherPlace;
  float weatherLatitude = 0.0f;
  float weatherLongitude = 0.0f;
  bool weatherHasLocation = false;
  bool weatherFahrenheit = false;

  // Image: BMP/PNG/JPEG files in this SD folder, one shown per interval.
  std::string carouselFolder = DEFAULT_CAROUSEL_FOLDER;
  uint16_t carouselIntervalMinutes = DEFAULT_CAROUSEL_MINUTES;

  static const char* getFilePath() { return "/.crosspoint/dashboards.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Validating setters shared by fromJson() and the web handler. Each returns
  // false and leaves the field alone when the value is out of range.
  bool setWeatherLocation(const char* place, float latitude, float longitude);
  void clearWeatherLocation();
  bool setCarouselFolder(const char* folder);
  bool setCarouselIntervalMinutes(int minutes);

  // "weather", "todo", "image", "date", "none"; nullptr/false on unknown input.
  static const char* widgetKey(WidgetId widget);
  static bool widgetFromKey(const char* key, WidgetId& out);

  const CustomLayout* findLayout(uint8_t id) const;
  const CustomLayout* activeLayout() const { return activeLayoutId ? findLayout(activeLayoutId) : nullptr; }
  bool activateLayout(uint8_t id);
  // Adds layout (id 0 takes the next free id, written back) or replaces the
  // one with its id. False when it is invalid or the list is full.
  bool saveLayout(CustomLayout& layout);
  // Removes it; when it was in use, the first remaining layout takes over.
  bool deleteLayout(uint8_t id);
  // Adds back each starting layout (Wide top, Grid, Picture top) whose name is
  // missing, while there is room, in the given shape. Returns how many were added.
  uint8_t restoreDefaultLayouts(bool portrait = false);

  // A layout's tiles on a dashboard of this shape (turned on their side when
  // it was drawn for the other shape), in reading order. Returns the count
  // (up to CustomLayout::MAX_TILES; 0 for an unknown id).
  uint8_t layoutTiles(uint8_t id, bool portrait, DashboardTile* out) const;
  uint8_t activeTiles(bool portrait, DashboardTile* out) const { return layoutTiles(activeLayoutId, portrait, out); }
  // True when some tile of the layout in use holds this widget.
  bool activeLayoutShows(WidgetId widget) const;

 private:
  // Files written before every layout was editable: presets A/B/C (built-in
  // geometry, per-preset widgets, optional edited tiles) beside the custom list.
  void migratePresets(JsonVariantConst doc);
};

#define DASHBOARD_CONFIG DashboardConfigStore::getInstance()
