#include "DictionaryChooserActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>
#include <utility>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

DictionaryChooserActivity::DictionaryChooserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     std::vector<DictionaryEntry>&& dictionaries)
    : UiListActivity("DictionaryChooser", renderer, mappedInput), dictionaries(std::move(dictionaries)) {}

void DictionaryChooserActivity::onEnter() {
  UiListActivity::onEnter();

  // Labels point into `dictionaries`, which is not modified while the activity lives.
  rowItems.clear();
  rowItems.reserve(dictionaries.size());
  for (size_t i = 0; i < dictionaries.size(); ++i) {
    fui::ListItem item;
    item.label = dictionaries[i].name.c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

const char* DictionaryChooserActivity::headerTitle() const { return tr(STR_CHOOSE_DICTIONARY); }

void DictionaryChooserActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(dictionaries.size())) return;
  app.clearTapFlash();
  nav.selected = index;

  strncpy(SETTINGS.dictionaryName, dictionaries[index].name.c_str(), sizeof(SETTINGS.dictionaryName) - 1);
  SETTINGS.dictionaryName[sizeof(SETTINGS.dictionaryName) - 1] = '\0';
  SETTINGS.saveToFile();
  finish();
}

void DictionaryChooserActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
