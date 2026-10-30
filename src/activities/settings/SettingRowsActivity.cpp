#include "SettingRowsActivity.h"

#include <Logging.h>

#include <utility>

#include "MappedInputManager.h"
#include "SettingsList.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

SettingRowsActivity::SettingRowsActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         const StrId title, const Field* fields, const int fieldCount)
    : UiListActivity(name, renderer, mappedInput), title(title), fields(fields), fieldCount(fieldCount) {}

void SettingRowsActivity::onEnter() {
  UiListActivity::onEnter();

  // One copy of the shared list; keep only this page's rows, in page order.
  infos.clear();
  infos.reserve(fieldCount);
  const auto all = getSettingsList();
  for (int i = 0; i < fieldCount && i < MAX_ROWS; i++) {
    bool found = false;
    for (const auto& info : all) {
      if (info.valuePtr != fields[i]) continue;
      infos.push_back(info);
      found = true;
      break;
    }
    if (!found) LOG_ERR("SETROWS", "No SettingInfo for row %d", i);
  }

  // Labels are fixed for the page's lifetime; values are refreshed per build.
  for (size_t i = 0; i < infos.size(); i++) {
    rowItems[i].label = I18N.get(infos[i].nameId);
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
}

void SettingRowsActivity::onExit() {
  UiListActivity::onExit();
  std::vector<SettingInfo>().swap(infos);
}

bool SettingRowsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void SettingRowsActivity::applyValue(const SettingInfo& info, const uint8_t value) {
  SETTINGS.*(info.valuePtr) = value;
  onSettingChanged(info.valuePtr);
  SETTINGS.saveToFile();
}

void SettingRowsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount() || optionPopup.isActive()) return;
  mappedInput.resetHomeButtonInput();
  app.clearTapFlash();
  nav.selected = index;

  const SettingInfo& info = infos[index];
  const uint8_t current = SETTINGS.*(info.valuePtr);
  if (info.type == SettingType::TOGGLE) {
    applyValue(info, current ? 0 : 1);
  } else if (info.type == SettingType::ENUM) {
    const auto labels = info.enumLabels();
    if (labels.size() > 2) {
      // The popup outlives this call, so it captures the field, not the row.
      const Field field = info.valuePtr;
      optionPopup.show(info.nameId, labels.data(), static_cast<int>(labels.size()), current, [this, field](int idx) {
        SETTINGS.*field = static_cast<uint8_t>(idx);
        onSettingChanged(field);
        SETTINGS.saveToFile();
      });
    } else if (!labels.empty()) {
      applyValue(info, static_cast<uint8_t>((current + 1) % labels.size()));
    }
  }
  requestUpdate();
}

void SettingRowsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

void SettingRowsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  for (size_t i = 0; i < infos.size(); i++) {
    const SettingInfo& info = infos[i];
    const uint8_t value = SETTINGS.*(info.valuePtr);
    if (info.type == SettingType::TOGGLE) {
      rowValues[i] = value ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    } else {
      const auto labels = info.enumLabels();
      // A corrupt byte must not index past the label table.
      rowValues[i] = value < labels.size() ? I18N.get(labels[value]) : "";
    }
    rowItems[i].value = rowValues[i].empty() ? nullptr : rowValues[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(infos.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;  // also marks the style as explicitly set
  syncListViewport(screen, props);
  screen.list(props);
}
