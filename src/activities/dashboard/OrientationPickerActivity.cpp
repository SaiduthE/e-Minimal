#include "OrientationPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/icons/dashboardOrientationIcons.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
struct Option {
  StrId name;
  StrId keys;
  const uint8_t* icon;
};

// Indexed by CrossPointSettings::DASHBOARD_ORIENTATION.
constexpr Option OPTIONS[CrossPointSettings::DASHBOARD_ORIENTATION_COUNT] = {
    {StrId::STR_LANDSCAPE_CW, StrId::STR_DASH_KEYS_LEFT, DashOrientKeysLeftIcon},
    {StrId::STR_LANDSCAPE_CCW, StrId::STR_DASH_KEYS_RIGHT, DashOrientKeysRightIcon},
    {StrId::STR_PORTRAIT, StrId::STR_DASH_KEYS_BOTTOM, DashOrientKeysBottomIcon},
};

// The icon's drawn size at 1x; the panel's UI scale brings it to the bitmap's 96.
constexpr int ICON_SIZE = 64;

fui::BitmapRef iconFor(const uint8_t* bits) {
  fui::BitmapRef ref;
  ref.data = bits;
  ref.width = DASH_ORIENT_ICON_SIZE;
  ref.height = DASH_ORIENT_ICON_SIZE;
  ref.format = fui::BitmapFormat::BW1;
  return ref;
}

uint8_t currentOrientation() {
  return SETTINGS.dashboardOrientation < CrossPointSettings::DASHBOARD_ORIENTATION_COUNT
             ? SETTINGS.dashboardOrientation
             : static_cast<uint8_t>(CrossPointSettings::DASHBOARD_LANDSCAPE_CW);
}
}  // namespace

const char* OrientationPickerActivity::orientationName(const uint8_t orientation) {
  return I18N.get(OPTIONS[orientation < OPTION_COUNT ? orientation : 0].name);
}

void OrientationPickerActivity::onEnter() {
  const uint8_t current = currentOrientation();
  {
    RenderLock lock;
    for (int i = 0; i < OPTION_COUNT; i++) {
      fui::ListItem item;
      item.label = I18N.get(OPTIONS[i].name);
      item.subtitle = I18N.get(OPTIONS[i].keys);
      item.value = i == current ? tr(STR_DASH_IN_USE) : nullptr;
      item.icon = iconFor(OPTIONS[i].icon);
      item.actionValue = static_cast<int16_t>(i);
      rowItems_[i] = item;
    }
  }
  UiListActivity::onEnter();
  moveSelectionTo(current);
}

const char* OrientationPickerActivity::headerTitle() const { return tr(STR_DASH_ORIENTATION); }

void OrientationPickerActivity::activateIndex(const int index) {
  if (index < 0 || index >= OPTION_COUNT) return;
  // Selection leaves this screen; a lingering flash would gray an unrelated
  // element on the next render.
  app.clearTapFlash();
  nav.selected = index;
  if (SETTINGS.dashboardOrientation != index) {
    SETTINGS.dashboardOrientation = static_cast<uint8_t>(index);
    if (!SETTINGS.saveToFile()) LOG_ERR("DASH", "Could not save the dashboard orientation");
  }
  finish();
}

void OrientationPickerActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});

  const int helpLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const fui::Rect hint = screen.takeBottom(static_cast<int16_t>(helpLineHeight + metrics.verticalSpacing));
  GUI.drawHelpText(renderer, Rect{hint.x, hint.y + metrics.verticalSpacing, hint.width, helpLineHeight},
                   tr(STR_DASH_ORIENTATION_HINT));
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(OPTION_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  // Drawn in the row's foreground, so the selected (inverted) row shows it in paper.
  props.iconSize = static_cast<int16_t>(UITheme::scaledPx(ICON_SIZE));
  syncListViewport(screen, props);
  screen.list(props);
}
