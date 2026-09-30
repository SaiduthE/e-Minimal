#pragma once

#include <GfxRenderer.h>

#include <cstdint>
#include <ctime>

#include "CarouselImages.h"
#include "DashboardImage.h"
#include "TodoStore.h"
#include "WeatherService.h"
#include "components/themes/BaseTheme.h"
#include "fontIds.h"

// The tiled dashboard's widgets, each painting itself into its tile's inner
// rectangle and laying itself out by the tile's shape and scale (TileSize).
// The tile frame and focus are the dashboard's. One file per widget:
// WeatherTile.cpp, TodoTile.cpp, DateTile.cpp; the image and the shared
// pieces are here.
namespace dashboard_tiles {

// A tile's layout class. Shape by aspect: Wide from 1.8:1, Tall from 1:1.6,
// else Square. Scale by the short side: Small under SMALL_MAX px, Large from
// LARGE_MIN px, else Medium. (A 2-cell side is ~216 px inside the frame, 3
// cells ~344, 5 cells ~600, 6 cells ~728.)
struct TileSize {
  enum class Shape : uint8_t { Square, Wide, Tall };
  enum class Scale : uint8_t { Small, Medium, Large };
  static constexpr int SMALL_MAX = 360;
  static constexpr int LARGE_MIN = 620;

  Shape shape = Shape::Square;
  Scale scale = Scale::Medium;
};
TileSize sizeOf(const Rect& area);

// The To-do widget's copy of the list: counts and the first open tasks, so
// TodoStore can be unloaded while the dashboard shows.
struct TodoSnapshot {
  static constexpr uint8_t MAX_SHOWN = 12;
  uint8_t open = 0;
  uint8_t done = 0;
  uint8_t shown = 0;
  char tasks[MAX_SHOWN][TodoStore::MAX_TEXT_LENGTH + 1] = {};

  // Loads TodoStore, copies under the render lock, unloads.
  void load();
};

void drawWeather(GfxRenderer& renderer, const Rect& area, const WeatherService& weather);
void drawTodo(GfxRenderer& renderer, const Rect& area, const TodoSnapshot& todo);

// How the Image tile got its pixels on a paint.
enum class ImagePaint : uint8_t { Note, Cached, Decoded };
// The Image widget, full bleed: the picture fills tile (the frame's rect) edge
// to edge, cropped to its shape and enlarged when small, with the corners
// outside the frame's radius whitened; the dashboard draws the frame over it.
// The finished tile goes into cache, so a repaint blits it back without
// touching the SD card. A note inside inner when the folder is empty or the
// picture cannot be read.
ImagePaint drawImage(GfxRenderer& renderer, const Rect& tile, const Rect& inner, int radius,
                     const CarouselImages& images, dashboard_image::TileCache& cache);
// The clock's local time comes from the forecast's offset when there is one.
void drawDate(GfxRenderer& renderer, const Rect& area, const WeatherService& weather);

// The dashboard's wall clock, when the system clock has been set.
bool localNow(const WeatherService& weather, tm& out);

// Painting pieces the widgets share.
namespace ui {
// The largest built-in face, for a temperature or a day number.
constexpr int BIG_FONT_ID = NOTOSERIF_24_FONT_ID;
constexpr int HERO_ICON = 160;
constexpr int SMALL_ICON = 96;

int rounded(float value);
void drawCentered(const GfxRenderer& renderer, int fontId, int centerX, int y, const char* text, bool bold = false);
void drawRight(const GfxRenderer& renderer, int fontId, int right, int y, const char* text);
// A wrapped message centred in what is left of the tile below top.
void drawNote(const GfxRenderer& renderer, const Rect& area, int top, const char* text);
// Title on the left, a short note on the right, a rule under both. Returns the y below.
int drawTitle(const GfxRenderer& renderer, const Rect& area, const char* title, const char* note);
}  // namespace ui

}  // namespace dashboard_tiles
