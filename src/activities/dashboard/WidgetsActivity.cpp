#include "WidgetsActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <string_view>

#include "DashboardImage.h"
#include "DashboardPortalActivity.h"
#include "MappedInputManager.h"
#include "TodoStore.h"
#include "WeatherForecast.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
using Section = DashboardPortalActivity::Section;

// Row order.
constexpr WidgetId WIDGETS[dashboard_widgets::COUNT] = {WidgetId::WEATHER, WidgetId::TODO, WidgetId::IMAGE,
                                                        WidgetId::DATE};
constexpr Section SECTIONS[dashboard_widgets::COUNT] = {Section::Weather, Section::Todo, Section::Image, Section::Date};
constexpr uint16_t MAX_COUNTED_IMAGES = 999;
constexpr size_t NAME_LENGTH = 256;

// The pictures in folder (dashboard_image::isImage), up to limit; names are not kept.
uint16_t countImages(const char* folder, const uint16_t limit) {
  if (limit == 0) return 0;
  auto dir = Storage.open(folder);
  if (!dir || !dir.isDirectory()) return 0;
  auto name = makeUniqueNoThrow<char[]>(NAME_LENGTH);
  if (!name) {
    LOG_ERR("DASH", "OOM: %u bytes for image names", static_cast<unsigned>(NAME_LENGTH));
    return 0;
  }
  uint16_t count = 0;
  for (auto file = dir.openNextFile(); file && count < limit; file = dir.openNextFile()) {
    if (file.isDirectory()) continue;
    file.getName(name.get(), NAME_LENGTH);
    if (dashboard_image::isImage(name.get())) count++;
  }
  return count;
}
}  // namespace

namespace dashboard_widgets {

uint8_t Status::readyCount() const {
  uint8_t ready = 0;
  if (hasPlace) ready++;
  if (todoTotal > 0) ready++;
  if (images > 0) ready++;
  if (clockSet) ready++;
  return ready;
}

Status readStatus(const uint16_t maxImages) {
  Status status;
  const auto& config = DASHBOARD_CONFIG;
  status.hasPlace = config.weatherHasLocation;

  TODO_STORE.load();
  for (const auto& item : TODO_STORE.getItems()) {
    status.todoTotal++;
    if (!item.done) status.todoOpen++;
  }
  TODO_STORE.unload();

  status.images = countImages(config.carouselFolder.c_str(), maxImages);
  status.clockSet = weather::clockValid();
  return status;
}

const char* widgetName(const WidgetId widget) {
  switch (widget) {
    case WidgetId::WEATHER:
      return tr(STR_DASH_WEATHER);
    case WidgetId::TODO:
      return tr(STR_TODO);
    case WidgetId::IMAGE:
      return tr(STR_DASH_IMAGE);
    case WidgetId::DATE:
      return tr(STR_DASH_DATE);
    default:
      return tr(STR_DASH_EMPTY);
  }
}

}  // namespace dashboard_widgets

void WidgetsActivity::onEnter() {
  refreshRows();
  UiListActivity::onEnter();
}

const char* WidgetsActivity::headerTitle() const { return tr(STR_DASH_WIDGETS); }

void WidgetsActivity::refreshRows() {
  using dashboard_widgets::COUNT;
  const auto status = dashboard_widgets::readStatus(MAX_COUNTED_IMAGES);
  const auto& config = DASHBOARD_CONFIG;

  RenderLock lock;
  for (uint8_t i = 0; i < COUNT; i++) {
    char* value = values_[i];
    switch (WIDGETS[i]) {
      case WidgetId::WEATHER:
        if (!status.hasPlace) {
          snprintf(value, VALUE_LENGTH, "%s", tr(STR_DASH_NEEDS_PLACE));
        } else if (config.weatherPlace.empty()) {
          // A location saved without a name still works.
          snprintf(value, VALUE_LENGTH, "%s", tr(STR_DASH_READY));
        } else {
          snprintf(value, VALUE_LENGTH, "%s", config.weatherPlace.c_str());
        }
        break;
      case WidgetId::TODO:
        if (status.todoTotal == 0) {
          snprintf(value, VALUE_LENGTH, "%s", tr(STR_DASH_EMPTY));
        } else {
          snprintf(value, VALUE_LENGTH, tr(STR_DASH_TODO_OPEN_FORMAT), static_cast<unsigned>(status.todoOpen));
        }
        break;
      case WidgetId::IMAGE:
        if (status.images == 0) {
          snprintf(value, VALUE_LENGTH, tr(STR_DASH_NO_IMAGES_FORMAT), config.carouselFolder.c_str());
        } else if (status.images == 1) {
          snprintf(value, VALUE_LENGTH, tr(STR_DASH_IMAGE_ONE_FORMAT), config.carouselFolder.c_str());
        } else {
          snprintf(value, VALUE_LENGTH, tr(STR_DASH_IMAGE_COUNT_FORMAT), config.carouselFolder.c_str(),
                   static_cast<unsigned>(status.images));
        }
        break;
      case WidgetId::DATE:
      default:
        snprintf(value, VALUE_LENGTH, "%s", status.clockSet ? tr(STR_DASH_READY) : tr(STR_DASH_DATE_WAITS));
        break;
    }

    fui::ListItem item;
    item.label = dashboard_widgets::widgetName(WIDGETS[i]);
    item.value = value;
    item.subtitle = config.activeLayoutShows(WIDGETS[i]) ? tr(STR_DASH_ON_LAYOUT) : tr(STR_DASH_OFF_LAYOUT);
    item.actionValue = static_cast<int16_t>(i);
    rowItems_[i] = item;
  }
}

void WidgetsActivity::activateIndex(const int index) {
  if (index < 0 || index >= dashboard_widgets::COUNT) return;
  // Selection leaves this screen; a lingering flash would gray an unrelated
  // element on the next render.
  app.clearTapFlash();
  nav.selected = index;
  auto portal = makeUniqueNoThrow<DashboardPortalActivity>(renderer, mappedInput, SECTIONS[index]);
  if (!portal) {
    LOG_ERR("DASH", "OOM: dashboard portal");
    return;
  }
  // Leaving the portal restarts into the Dashboard app, so no result comes back.
  startActivityForResult(std::move(portal), [](const ActivityResult&) {});
}

void WidgetsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});

  const int helpLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const fui::Rect hint = screen.takeBottom(static_cast<int16_t>(helpLineHeight + metrics.verticalSpacing));
  GUI.drawHelpText(renderer, Rect{hint.x, hint.y + metrics.verticalSpacing, hint.width, helpLineHeight},
                   tr(STR_DASH_WIDGETS_HINT));
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(dashboard_widgets::COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  syncListViewport(screen, props);
  screen.list(props);
}
