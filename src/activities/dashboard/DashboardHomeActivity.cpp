#include "DashboardHomeActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "DashboardConfigStore.h"
#include "LayoutPickerActivity.h"
#include "MappedInputManager.h"
#include "OrientationPickerActivity.h"
#include "SilentRestart.h"
#include "TiledDashboardActivity.h"
#include "WidgetsActivity.h"
#include "components/UITheme.h"
#include "network/SavedWifi.h"

namespace fui = freeink::ui;

void DashboardHomeActivity::onEnter() {
  {
    RenderLock lock;
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  }
  DASHBOARD_CONFIG.loadFromFile();
  refreshRows();
  // After a wake the panel still shows the sleep image; a clean refresh clears it.
  renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
  UiListActivity::onEnter();
}

void DashboardHomeActivity::onExit() {
  UiListActivity::onExit();
  // The radio leaves the heap fragmented enough to hurt the reader; restart the
  // way the other WiFi screens do, into the book when that is where this goes.
  if (saved_wifi::usedSinceBoot()) {
    if (activityManager.isEnteringReader()) {
      silentRestartToReader();
    } else {
      silentRestart();
    }
  }
}

const char* DashboardHomeActivity::headerTitle() const { return tr(STR_APP_DASHBOARD); }

void DashboardHomeActivity::drawFooter() {
  // No Back: the power menu switches apps.
  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void DashboardHomeActivity::refreshRows() {
  // Set up or not needs no more than one image; the count reads the card, so
  // it comes before the lock.
  const auto status = dashboard_widgets::readStatus(1);
  const auto& config = DASHBOARD_CONFIG;

  RenderLock lock;
  const char* layout = dashboard_layouts::activeName(config);
  snprintf(values_[LAUNCH], VALUE_LENGTH, "%s", layout);
  snprintf(values_[LAYOUT], VALUE_LENGTH, "%s", layout);
  snprintf(values_[WIDGETS], VALUE_LENGTH, tr(STR_DASH_WIDGETS_READY_FORMAT),
           static_cast<unsigned>(status.readyCount()), static_cast<unsigned>(dashboard_widgets::COUNT));
  snprintf(values_[ORIENTATION], VALUE_LENGTH, "%s",
           OrientationPickerActivity::orientationName(SETTINGS.dashboardOrientation));

  static constexpr StrId LABELS[ROW_COUNT] = {StrId::STR_DASH_LAUNCH, StrId::STR_DASH_CHANGE_LAYOUT,
                                              StrId::STR_DASH_WIDGETS, StrId::STR_DASH_ORIENTATION};
  for (int i = 0; i < ROW_COUNT; i++) {
    fui::ListItem item;
    item.label = I18N.get(LABELS[i]);
    item.value = values_[i];
    item.actionValue = static_cast<int16_t>(i);
    rowItems_[i] = item;
  }
}

void DashboardHomeActivity::open(std::unique_ptr<Activity> screen, const bool turned) {
  if (!screen) {
    LOG_ERR("DASH", "OOM: dashboard screen");
    return;
  }
  startActivityForResult(std::move(screen), [this, turned](const ActivityResult&) {
    if (turned) {
      // The dashboard turns itself back; make sure before painting.
      RenderLock lock;
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    }
    refreshRows();
    // Back from another orientation, a fast refresh would leave the old one ghosted.
    if (turned) renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
    requestUpdate();
  });
}

void DashboardHomeActivity::activateIndex(const int index) {
  // Selection leaves this screen; a lingering flash would gray an unrelated
  // element on the next render.
  app.clearTapFlash();
  nav.selected = index;
  switch (index) {
    case LAUNCH:
      open(makeUniqueNoThrow<TiledDashboardActivity>(renderer, mappedInput), true);
      return;
    case LAYOUT:
      open(makeUniqueNoThrow<LayoutPickerActivity>(renderer, mappedInput));
      return;
    case WIDGETS:
      open(makeUniqueNoThrow<WidgetsActivity>(renderer, mappedInput));
      return;
    case ORIENTATION:
      open(makeUniqueNoThrow<OrientationPickerActivity>(renderer, mappedInput));
      return;
    default:
      return;
  }
}

void DashboardHomeActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(ROW_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}
