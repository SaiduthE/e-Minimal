#include "EpubReaderHighlightsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long DELETE_HOLD_MS = 700;
}  // namespace

EpubReaderHighlightsActivity::EpubReaderHighlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                           const std::shared_ptr<Epub>& epub, HighlightStore& store)
    : UiListActivity("EpubReaderHighlights", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      epub(epub),
      store(store) {}

void EpubReaderHighlightsActivity::onEnter() {
  UiListActivity::onEnter();
  rebuildRows();
}

// Snippet as the label, chapter title as the subtitle.
void EpubReaderHighlightsActivity::rebuildRows() {
  subtitles.clear();
  rows.clear();
  subtitles.reserve(store.size());
  rows.reserve(store.size());
  for (size_t i = 0; i < store.size(); i++) {
    const auto& highlight = store.at(i);
    const int tocIndex = epub ? epub->getTocIndexForSpineIndex(highlight.spine) : -1;
    subtitles.emplace_back(tocIndex >= 0 ? epub->getTocItem(tocIndex).title : tr(STR_UNNAMED));

    fui::ListItem item;
    item.label = highlight.snippet;
    item.subtitle = subtitles.back().c_str();
    item.icon = listIconFor(UIIcon::Bookmark, 32);
    item.actionValue = static_cast<int16_t>(i);
    rows.push_back(item);
  }
}

void EpubReaderHighlightsActivity::openSelected() {
  if (nav.selected < 0 || nav.selected >= listCount()) return;
  const auto& highlight = store.at(nav.selected);
  ProgressChangeResult result{};
  result.spineIndex = highlight.spine;
  result.hasVisibleTextOffset = true;
  result.visibleTextOffset = highlight.start;
  setResult(std::move(result));
  finish();
}

void EpubReaderHighlightsActivity::deleteSelected() {
  if (nav.selected < 0 || nav.selected >= listCount()) return;
  if (!store.remove(static_cast<size_t>(nav.selected))) {
    LOG_ERR("EHL", "Failed to delete highlight");
    return;
  }
  rebuildRows();
  if (nav.selected >= listCount() && nav.selected > 0) nav.selected--;
  nav.follow(listCount());
  showToast = true;
  toastTime = millis();
  requestUpdate(true);
}

void EpubReaderHighlightsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  openSelected();
}

void EpubReaderHighlightsActivity::onRowLongPress(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  deleteSelected();
}

bool EpubReaderHighlightsActivity::handleCustomInput() {
  if (showToast && millis() - toastTime >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showToast = false;
    requestUpdate();
  }
  return false;
}

bool EpubReaderHighlightsActivity::handleButtons() {
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, DELETE_HOLD_MS)) {
    deleteSelected();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected();
    return true;
  }
  return false;
}

void EpubReaderHighlightsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (store.empty()) {
    screen.centeredText(tr(STR_NO_HIGHLIGHTS), screen.theme().bodyText);
    return;
  }

  // This hint names a physical button, so omit it on touch boards. The band is
  // taken off the bottom, so it sits just above the button hints.
  if (!mappedInput.hasTouch()) {
    const int helpLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect band = screen.takeBottom(static_cast<int16_t>(helpLineHeight + metrics.verticalSpacing));
    GUI.drawHelpText(renderer, Rect{band.x, band.y + metrics.verticalSpacing, band.width, helpLineHeight},
                     tr(STR_HOLD_OPEN_TO_DELETE));
  }

  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  syncListViewport(screen, props);
  screen.list(props);
}

void EpubReaderHighlightsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const int titleX = (renderer.getScreenWidth() -
                      renderer.getTextWidth(UI_12_FONT_ID, tr(STR_HIGHLIGHTS), EpdFontFamily::BOLD)) /
                     2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15, tr(STR_HIGHLIGHTS), true, EpdFontFamily::BOLD);

  renderUi();

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), store.empty() ? "" : tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (showToast) {
    // drawPopup overlays the framebuffer and refreshes the display itself.
    GUI.drawPopup(renderer, tr(STR_HIGHLIGHT_REMOVED));
    return;
  }
  renderer.displayBuffer();
}
