#include "TiledDashboardActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "CarouselDashboardActivity.h"
#include "DashboardGrid.h"
#include "MappedInputManager.h"
#include "TodoDashboardActivity.h"
#include "WeatherDashboardActivity.h"
#include "components/UITheme.h"

namespace {
bool isDue(const unsigned long at, const unsigned long now) {
  return at != 0 && static_cast<long>(now - at) >= 0;
}
}  // namespace

void TiledDashboardActivity::onEnter() {
  TimedDashboardActivity::onEnter();
  portrait = dashboard_grid::applyOrientation(renderer);
  showFocus = true;
  loadWidgets();
  focused = 0;
  requestContentRepaint();
}

void TiledDashboardActivity::onExit() {
  Activity::onExit();
  images.unload();
  // Called under the render lock, like draw().
  for (auto& cache : imageCaches) cache.release();
  // The rest of the UI is portrait.
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
}

void TiledDashboardActivity::requestContentRepaint(const bool immediate) {
  contentVersion.fetch_add(1);
  focusOnlyRepaint = false;
  requestUpdate(immediate);
}

bool TiledDashboardActivity::shows(const WidgetId widget) const {
  for (uint8_t i = 0; i < tileCount; i++) {
    if (tiles[i].widget == widget) return true;
  }
  return false;
}

bool TiledDashboardActivity::wantsWeather() const {
  return shows(WidgetId::WEATHER) || (shows(WidgetId::DATE) && !weather::clockValid());
}

void TiledDashboardActivity::loadWidgets() {
  DASHBOARD_CONFIG.loadFromFile();
  {
    RenderLock lock;
    tileCount = DASHBOARD_CONFIG.activeTiles(portrait, tiles);
    if (focused >= tileCount) focused = 0;
    // The layout, the folder or a picture may have changed under the tiles;
    // slots that no longer hold an Image tile give their PSRAM back.
    for (uint8_t i = 0; i < CustomLayout::MAX_TILES; i++) {
      if (i < tileCount && tiles[i].widget == WidgetId::IMAGE) {
        imageCaches[i].invalidate();
      } else {
        imageCaches[i].release();
      }
    }
  }
  // The date tile reads its time zone from the forecast, so it loads weather too.
  if (shows(WidgetId::WEATHER) || shows(WidgetId::DATE)) weather.load();
  if (shows(WidgetId::IMAGE)) {
    images.load();
  } else {
    RenderLock lock;
    images.unload();
  }
  if (shows(WidgetId::TODO)) todo.load();
  dateChange = shows(WidgetId::DATE) ? nextDateChangeMs() : 0;
}

unsigned long TiledDashboardActivity::nextDateChangeMs() const {
  tm local{};
  if (!dashboard_tiles::localNow(weather, local)) return 0;
  const long secondsIntoDay = local.tm_hour * 3600L + local.tm_min * 60L + local.tm_sec;
  // A few seconds past midnight, so the new day is certain.
  const long wait = 24 * 3600L - secondsIntoDay + 5;
  return millis() + static_cast<unsigned long>(wait) * 1000UL;
}

unsigned long TiledDashboardActivity::nextUpdateMs() const {
  const unsigned long now = millis();
  unsigned long earliest = 0;
  const auto consider = [&earliest, now](const unsigned long at) {
    if (at == 0) return;
    if (earliest == 0 || static_cast<long>(at - now) < static_cast<long>(earliest - now)) earliest = at;
  };
  if (wantsWeather()) consider(weather.nextFetchMs());
  if (shows(WidgetId::IMAGE)) consider(images.nextChangeMs());
  if (shows(WidgetId::DATE)) consider(dateChange);
  return earliest;
}

void TiledDashboardActivity::update() {
  const unsigned long now = millis();
  bool changed = false;
  if (wantsWeather() && isDue(weather.nextFetchMs(), now)) {
    if (shows(WidgetId::WEATHER) && !weather.forecast().valid) {
      weather.markUpdating();
      // Immediate: the loop is about to block, so a deferred request would wait for it.
      requestContentRepaint(true);
    }
    changed |= weather.fetch();
    // The fetch may have set the clock the date tile waits for.
    if (shows(WidgetId::DATE)) dateChange = nextDateChangeMs();
  }
  if (shows(WidgetId::IMAGE) && isDue(images.nextChangeMs(), now)) {
    {
      // step() takes the render lock itself.
      RenderLock lock;
      for (auto& cache : imageCaches) cache.invalidate();
    }
    images.step(1);
    changed = true;
  }
  if (shows(WidgetId::DATE) && isDue(dateChange, now)) {
    dateChange = nextDateChangeMs();
    changed = true;
  }
  if (changed) requestContentRepaint();
}

bool TiledDashboardActivity::prepareStandby() {
  if (!showFocus) return false;
  showFocus = false;
  focusOnlyRepaint = true;
  requestUpdate();
  return true;
}

void TiledDashboardActivity::onWake(const bool /*byKey*/) {
  showFocus = true;
  // A timer wake's update may already have asked for a repaint; the frame rides on it.
  if (repaintPending()) return;
  focusOnlyRepaint = true;
  requestUpdate();
}

void TiledDashboardActivity::moveFocus(const int delta) {
  if (tileCount == 0) return;
  const int next = ((focused + delta) % tileCount + tileCount) % tileCount;
  if (next == focused) return;
  focused = next;
  focusOnlyRepaint = true;
  requestUpdate();
}

void TiledDashboardActivity::openFocused() {
  if (focused >= tileCount) return;
  std::unique_ptr<Activity> view;
  switch (tiles[focused].widget) {
    case WidgetId::WEATHER:
      view = makeUniqueNoThrow<WeatherDashboardActivity>(renderer, mappedInput);
      break;
    case WidgetId::TODO:
      view = makeUniqueNoThrow<TodoDashboardActivity>(renderer, mappedInput);
      break;
    case WidgetId::IMAGE:
      view = makeUniqueNoThrow<CarouselDashboardActivity>(renderer, mappedInput);
      break;
    default:
      return;  // the date tile has no view of its own
  }
  if (!view) {
    LOG_ERR("DASH", "OOM: dashboard view");
    return;
  }
  startActivityForResult(std::move(view), [this](const ActivityResult&) {
    // The view may have fetched, stepped the images or edited the list.
    loadWidgets();
    noteActivity();
    showFocus = true;
    requestContentRepaint();
  });
}

bool TiledDashboardActivity::handleKeys() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Down)) {
    moveFocus(1);
    return true;
  }
  if (mappedInput.wasReleased(Button::Up)) {
    moveFocus(-1);
    return true;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    openFocused();
    return true;
  }
  return false;
}

TiledDashboardActivity::PaintStats TiledDashboardActivity::paintTiles() {
  PaintStats stats;
  renderer.clearScreen();
  if (tileCount == 0) {
    // Every layout was deleted.
    dashboard_tiles::ui::drawNote(renderer, Rect{0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()}, 0,
                                  tr(STR_DASH_NO_LAYOUT));
    return stats;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int radius = std::max(0, metrics.listRowRadius);
  // Half the gap between neighbouring tiles; inside the cells, so every tile
  // keeps to its own refresh blocks.
  const int inset = UITheme::scaledPx(6);
  const int pad = UITheme::scaledPx(14);

  for (uint8_t i = 0; i < tileCount; i++) {
    if (tiles[i].widget == WidgetId::NONE) continue;
    const Rect cells = dashboard_grid::tileRect(renderer, tiles[i]);
    const Rect tile{cells.x + inset, cells.y + inset, cells.width - inset * 2, cells.height - inset * 2};
    const Rect inner{tile.x + pad, tile.y + pad, tile.width - pad * 2, tile.height - pad * 2};
    switch (tiles[i].widget) {
      case WidgetId::WEATHER:
        dashboard_tiles::drawWeather(renderer, inner, weather);
        break;
      case WidgetId::TODO:
        dashboard_tiles::drawTodo(renderer, inner, todo);
        break;
      case WidgetId::IMAGE: {
        const unsigned long imageStart = millis();
        const auto paint = dashboard_tiles::drawImage(renderer, tile, inner, radius, images, imageCaches[i]);
        stats.imageMs += millis() - imageStart;
        // The costliest way any Image tile got its pixels.
        stats.image = std::max(stats.image, paint);
        break;
      }
      case WidgetId::DATE:
        dashboard_tiles::drawDate(renderer, inner, weather);
        break;
      default:
        break;
    }
    // Over the content: on the Image tile it covers the picture's edge.
    const int frame = showFocus && static_cast<int>(i) == focused ? UITheme::scaledPx(6) : 2;
    renderer.drawRoundedRect(tile.x, tile.y, tile.width, tile.height, frame, radius, true);
  }
  return stats;
}

void TiledDashboardActivity::logRepaint(const char* frame, const HalDisplay::RefreshMode mode,
                                        const PaintStats& paint, const unsigned long start,
                                        const unsigned long shownAt) {
  const unsigned long end = millis();
  const char* image = "none";
  if (paint.image == dashboard_tiles::ImagePaint::Cached) image = "blit";
  if (paint.image == dashboard_tiles::ImagePaint::Decoded) image = "decode";
  LOG_DBG("DASH", "Repaint %s %s: paint=%lums (image %s %lums) display=%lums total=%lums", frame,
          mode == HalDisplay::FAST_REFRESH ? "fast" : "half", shownAt - start, image, paint.imageMs, end - shownAt,
          end - start);
  // Unused where LOG_DBG compiles out.
  (void)image;
  (void)end;
}

void TiledDashboardActivity::draw(RenderLock&&) {
  const unsigned long start = millis();
  // Content changes want a clean waveform; a focus change (a move, the frame
  // hidden for standby or back on wake) can be quick, unless content changed
  // since the last paint too. Read at the start, so a move made during this
  // paint keeps its quick repaint.
  const uint32_t version = contentVersion.load();
  const bool quick = focusOnlyRepaint.exchange(false) && version == paintedVersion;
  paintedVersion = version;
  auto mode = quick ? HalDisplay::FAST_REFRESH : HalDisplay::HALF_REFRESH;

  // A picture on screen: the whole frame goes through the 16-level target, so
  // it shows in grey; text and frames draw solid. Loaded whole on every paint.
  // Fast is DU4 there: only changed pixels are driven, so a blitted photo
  // stays put while the focus frame moves (the driver's periodic GC16 clears).
  if (!gray4Refused && shows(WidgetId::IMAGE) && !images.empty() && renderer.supportsGray4() &&
      renderer.beginGray4Target(false)) {
    const PaintStats paint = paintTiles();
    renderer.endGray4Target();
    const unsigned long shownAt = millis();
    const bool shown = renderer.displayGray4Buffer(renderer.getGray4Buffer(), mode, /*updateFrameBuffer=*/true);
    renderer.releaseGray4Target();
    if (shown) {
      logRepaint("gray4", mode, paint, start, shownAt);
      return;
    }
    // Refused (the driver cannot take a 16-level frame): its thresholded base
    // would turn the photo into a black block, so paint this frame again black
    // and white (a dithered photo), and stay there for the rest of this visit.
    LOG_ERR("DASH", "gray4 display refused; showing the dashboard black and white");
    gray4Refused = true;
    mode = HalDisplay::HALF_REFRESH;
  }

  const PaintStats paint = paintTiles();
  const unsigned long shownAt = millis();
  renderer.displayBuffer(mode);
  logRepaint("1bpp", mode, paint, start, shownAt);
}
