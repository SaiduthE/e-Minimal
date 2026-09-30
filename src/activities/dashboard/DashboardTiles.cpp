#include "DashboardTiles.h"

#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "DashboardImage.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"

namespace dashboard_tiles {

TileSize sizeOf(const Rect& area) {
  TileSize size;
  const int shortSide = std::min(area.width, area.height);
  if (area.width * 10 >= area.height * 18) {
    size.shape = TileSize::Shape::Wide;
  } else if (area.height * 10 >= area.width * 16) {
    size.shape = TileSize::Shape::Tall;
  }
  if (shortSide < TileSize::SMALL_MAX) {
    size.scale = TileSize::Scale::Small;
  } else if (shortSide >= TileSize::LARGE_MIN) {
    size.scale = TileSize::Scale::Large;
  }
  return size;
}

namespace ui {

int rounded(const float value) { return static_cast<int>(std::lround(value)); }

void drawCentered(const GfxRenderer& renderer, const int fontId, const int centerX, const int y, const char* text,
                  const bool bold) {
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  renderer.drawText(fontId, centerX - renderer.getTextWidth(fontId, text, style) / 2, y, text, true, style);
}

void drawRight(const GfxRenderer& renderer, const int fontId, const int right, const int y, const char* text) {
  renderer.drawText(fontId, right - renderer.getTextWidth(fontId, text), y, text);
}

void drawNote(const GfxRenderer& renderer, const Rect& area, const int top, const char* text) {
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto lines = renderer.wrappedText(UI_10_FONT_ID, text, area.width, 4);
  int y = top + (area.y + area.height - top - static_cast<int>(lines.size()) * lineHeight) / 2;
  for (const auto& line : lines) {
    drawCentered(renderer, UI_10_FONT_ID, area.x + area.width / 2, y, line.c_str());
    y += lineHeight;
  }
}

int drawTitle(const GfxRenderer& renderer, const Rect& area, const char* title, const char* note) {
  const int uiLine = renderer.getLineHeight(UI_12_FONT_ID);
  const int noteWidth = note && note[0] ? renderer.getTextWidth(SMALL_FONT_ID, note) + UITheme::scaledPx(16) : 0;
  const std::string fitted =
      renderer.truncatedText(UI_12_FONT_ID, title, area.width - noteWidth, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, area.x, area.y, fitted.c_str(), true, EpdFontFamily::BOLD);
  if (noteWidth > 0) {
    const int smallLine = renderer.getLineHeight(SMALL_FONT_ID);
    drawRight(renderer, SMALL_FONT_ID, area.x + area.width, area.y + (uiLine - smallLine) / 2, note);
  }
  const int ruleY = area.y + uiLine + UITheme::scaledPx(4);
  renderer.drawLine(area.x, ruleY, area.x + area.width - 1, ruleY, 2, true);
  return ruleY + UITheme::scaledPx(16);
}

}  // namespace ui

void TodoSnapshot::load() {
  TODO_STORE.load();
  {
    RenderLock lock;
    open = 0;
    done = 0;
    shown = 0;
    for (const auto& item : TODO_STORE.getItems()) {
      if (item.done) {
        done++;
        continue;
      }
      open++;
      if (shown < MAX_SHOWN) {
        snprintf(tasks[shown], sizeof(tasks[0]), "%s", item.text.c_str());
        shown++;
      }
    }
  }
  TODO_STORE.unload();
}

bool localNow(const WeatherService& weather, tm& out) {
  if (!weather::clockValid()) return false;
  const time_t now = time(nullptr);
  if (weather.forecast().valid) {
    out = weather::localAt(now, weather.forecast().utcOffset);
  } else {
    localtime_r(&now, &out);
  }
  return true;
}

ImagePaint drawImage(GfxRenderer& renderer, const Rect& tile, const Rect& inner, const int radius,
                     const CarouselImages& images, dashboard_image::TileCache& cache) {
  char message[192];
  if (images.empty()) {
    snprintf(message, sizeof(message), tr(STR_DASH_IMAGE_EMPTY_FORMAT), images.folder().c_str());
    ui::drawNote(renderer, inner, inner.y, message);
    return ImagePaint::Note;
  }
  const std::string path = images.currentPath();
  if (cache.restore(renderer, tile, path)) return ImagePaint::Cached;
  // Cropped to fill the tile rather than letterboxed; the full-screen view
  // shows all of it.
  if (dashboard_image::draw(renderer, tile, path, dashboard_image::Fit::Fill)) {
    dashboard_image::maskCorners(renderer, tile, radius);
  } else {
    // Whatever part of the picture decoded goes; the note stands on white.
    renderer.fillRect(tile.x, tile.y, tile.width, tile.height, false);
    snprintf(message, sizeof(message), tr(STR_DASH_IMAGE_BAD_FORMAT), images.currentName().c_str());
    ui::drawNote(renderer, inner, inner.y, message);
  }
  // The note too, so a file that cannot be read is not retried on every focus move.
  cache.capture(renderer, tile, path);
  return ImagePaint::Decoded;
}

}  // namespace dashboard_tiles
