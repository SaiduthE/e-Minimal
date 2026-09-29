#include "PowerMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/icons/appIcons96.h"

namespace fui = freeink::ui;

namespace {
constexpr StrId menuItems[PowerMenuActivity::MENU_ITEM_COUNT] = {
    StrId::STR_SLEEP,
    StrId::STR_GO_HOME_BUTTON,
    StrId::STR_FORCE_REFRESH,
};

struct AppTile {
  AppId app;
  StrId name;
  const uint8_t* icon;
};

constexpr AppTile APP_TILES[PowerMenuActivity::APP_COUNT] = {
    {AppId::READER, StrId::STR_APP_READER, ReaderAppIcon96},
    {AppId::DASHBOARD, StrId::STR_APP_DASHBOARD, DashboardAppIcon96},
    {AppId::GAMES, StrId::STR_APP_GAMES, GamesAppIcon96},
};

constexpr int ICON_SIZE = 96;
}  // namespace

PowerMenuActivity::PowerMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("PowerMenu", renderer, mappedInput) {
  for (int i = 0; i < MENU_ITEM_COUNT; i++) {
    fui::ListItem item;
    item.label = I18N.get(menuItems[i]);
    item.actionValue = static_cast<int16_t>(i);
    rowItems_[i] = item;
  }
}

void PowerMenuActivity::onEnter() {
  currentApp = APP_STATE.lastApp;
  UiListActivity::onEnter();
  // Open on Sleep, so hold Power then Select still sleeps.
  nav.reset(APP_COUNT);
}

const char* PowerMenuActivity::headerTitle() const { return tr(STR_POWER_MENU); }

void PowerMenuActivity::activateApp(const AppId target) {
  app.clearTapFlash();
  if (target == currentApp) {
    // Already open underneath: nothing to switch.
    finish();
    return;
  }
  LOG_DBG("PWR", "Switching app %d -> %d", static_cast<int>(currentApp), static_cast<int>(target));
  activityManager.goToApp(target);
}

void PowerMenuActivity::activateIndex(const int index) {
  // Every row leaves this screen; a lingering flash would gray an unrelated
  // element on the next render.
  app.clearTapFlash();
  nav.selected = APP_COUNT + index;

  switch (static_cast<Action>(index)) {
    case Action::SLEEP:
      // Not finish(): the sleep screen replaces the whole stack, and popping
      // first would re-render the page underneath for nothing. The reader stays
      // on the stack, so the wake still resumes the book.
      activityManager.requestDeepSleep();
      return;
    case Action::GO_HOME:
      activityManager.goHome();
      return;
    case Action::REFRESH_SCREEN:
      // Applied by openPowerMenu()'s result handler on the screen underneath.
      setResult(MenuResult{static_cast<int>(Action::REFRESH_SCREEN)});
      finish();
      return;
  }
}

void PowerMenuActivity::onRowAction(const fui::ActionEvent& event) {
  // Touch reports the row; the ring keeps the tiles ahead of it.
  if (event.longPress) return;
  activateIndex(event.value);
}

bool PowerMenuActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBackButton();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int selected = nav.selected;
    if (selected >= 0 && selected < APP_COUNT) {
      activateApp(APP_TILES[selected].app);
    } else if (selected >= APP_COUNT && selected < RING_COUNT) {
      activateIndex(selected - APP_COUNT);
    }
    return true;
  }
  return false;
}

void PowerMenuActivity::navigateButtons() {
  buttonNavigator.onNextRelease([this] { moveSelectionTo(ButtonNavigator::nextIndex(nav.selected, RING_COUNT)); });
  buttonNavigator.onPreviousRelease(
      [this] { moveSelectionTo(ButtonNavigator::previousIndex(nav.selected, RING_COUNT)); });
}

int16_t PowerMenuActivity::tileBandHeight(UiScreen& screen) const {
  const auto& theme = screen.theme();
  const int16_t pad = static_cast<int16_t>(UITheme::scaledPx(16));
  return static_cast<int16_t>(pad + ICON_SIZE + theme.spaceSm + screen.target().lineHeight(theme.bodyText.font) +
                              screen.target().lineHeight(theme.smallText.font) + pad);
}

void PowerMenuActivity::drawAppTiles(UiScreen& screen, const fui::Rect band) {
  auto& target = screen.target();
  const auto& theme = screen.theme();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int16_t gap = static_cast<int16_t>(UITheme::scaledPx(16));
  const int16_t pad = static_cast<int16_t>(UITheme::scaledPx(16));
  const int16_t tileWidth = static_cast<int16_t>((band.width - gap * (APP_COUNT - 1)) / APP_COUNT);
  const uint8_t radius = static_cast<uint8_t>(std::clamp(metrics.listRowRadius, 0, 255));
  const int16_t labelHeight = target.lineHeight(theme.bodyText.font);
  const int16_t noteHeight = target.lineHeight(theme.smallText.font);

  for (int i = 0; i < APP_COUNT; i++) {
    const auto& tile = APP_TILES[i];
    const fui::Rect rect{static_cast<int16_t>(band.x + i * (tileWidth + gap)), band.y, tileWidth, band.height};
    const bool selected = nav.selected == i;
    const bool current = tile.app == currentApp;
    const fui::Color ink = selected ? fui::Color::White : fui::Color::Black;

    if (selected) {
      target.fill(rect, fui::Paint::solid(fui::Color::Black), radius);
    } else {
      // The open app keeps a heavier frame, so it reads as "here" without the cursor.
      target.stroke(rect, fui::Paint::solid(fui::Color::Black), current ? 4 : 2, radius);
    }

    int16_t y = static_cast<int16_t>(rect.y + pad);
    renderer.drawIcon(tile.icon, rect.x + (rect.width - ICON_SIZE) / 2, y, ICON_SIZE, !selected);
    y = static_cast<int16_t>(y + ICON_SIZE + theme.spaceSm);

    auto label = theme.bodyText;
    label.align = fui::TextAlign::Center;
    label.bold = true;
    label.color = ink;
    target.text(fui::Rect{rect.x, y, rect.width, labelHeight}, I18N.get(tile.name), label);
    y = static_cast<int16_t>(y + labelHeight);

    if (current) {
      auto note = theme.smallText;
      note.align = fui::TextAlign::Center;
      note.color = ink;
      target.text(fui::Rect{rect.x, y, rect.width, noteHeight}, tr(STR_APP_OPEN_NOW), note);
    }
  }
}

void PowerMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  const fui::Rect band = screen.takeTop(tileBandHeight(screen), static_cast<int16_t>(metrics.verticalSpacing * 2));
  const int16_t inset = static_cast<int16_t>(metrics.contentSidePadding);
  drawAppTiles(screen, fui::Rect{static_cast<int16_t>(band.x + inset), band.y,
                                 static_cast<int16_t>(band.width - inset * 2), band.height});

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEM_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props, APP_COUNT);
  screen.list(props);
}
