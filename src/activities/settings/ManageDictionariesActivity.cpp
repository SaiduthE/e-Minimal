#include "ManageDictionariesActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "activities/dashboard/DashboardPortalActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/TextEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long ACTIONS_HOLD_MS = 700;
constexpr unsigned long TOAST_MS = 2000;
constexpr const char* HIDDEN_ROOT = "/.dictionaries";
constexpr size_t MAX_NAME_LENGTH = sizeof(CrossPointSettings::dictionaryName) - 1;

enum Action { ACTION_USE, ACTION_RENAME, ACTION_DELETE, ACTION_ADD_MORE, ACTION_COUNT };
constexpr StrId ACTION_LABELS[ACTION_COUNT] = {StrId::STR_USE_AS_DEFAULT, StrId::STR_RENAME, StrId::STR_DELETE,
                                               StrId::STR_ADD_MORE};

// The stored name is truncated to the settings field, so compare within it.
bool isSelected(const std::string& name) {
  return SETTINGS.dictionaryName[0] != '\0' && strncmp(name.c_str(), SETTINGS.dictionaryName, MAX_NAME_LENGTH) == 0;
}

void storeSelection(const char* name) {
  strncpy(SETTINGS.dictionaryName, name, MAX_NAME_LENGTH);
  SETTINGS.dictionaryName[MAX_NAME_LENGTH] = '\0';
  SETTINGS.saveToFile();
}

std::string trimmed(const std::string& text) {
  const size_t first = text.find_first_not_of(' ');
  if (first == std::string::npos) return std::string();
  return text.substr(first, text.find_last_not_of(' ') - first + 1);
}
}  // namespace

ManageDictionariesActivity::ManageDictionariesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("ManageDictionaries", renderer, mappedInput) {}

void ManageDictionariesActivity::onEnter() {
  {
    RenderLock lock(*this);
    DictionaryRegistry::discover(dictionaries);
    rebuildRows();
  }
  UiListActivity::onEnter();
}

void ManageDictionariesActivity::onExit() {
  UiListActivity::onExit();
  rowItems.clear();
  rowItems.shrink_to_fit();
  subtitles.clear();
  subtitles.shrink_to_fit();
  dictionaries.clear();
  dictionaries.shrink_to_fit();
}

const char* ManageDictionariesActivity::headerTitle() const { return tr(STR_MANAGE_DICTIONARIES); }

void ManageDictionariesActivity::rebuildRows() {
  subtitles.clear();
  rowItems.clear();
  if (dictionaries.empty()) return;

  // reserve() keeps the subtitle strings in place, so the items can point at them.
  subtitles.reserve(dictionaries.size());
  rowItems.reserve(dictionaries.size() + 1);
  for (size_t i = 0; i < dictionaries.size(); ++i) {
    const DictionaryEntry& dictionary = dictionaries[i];
    std::string subtitle;
    if (isSelected(dictionary.name)) subtitle = tr(STR_IN_USE);
    if (strcmp(dictionary.root, HIDDEN_ROOT) == 0) {
      if (!subtitle.empty()) subtitle += " - ";
      subtitle += HIDDEN_ROOT;
    }
    subtitles.push_back(std::move(subtitle));

    fui::ListItem item;
    item.label = dictionary.name.c_str();
    item.subtitle = subtitles.back().empty() ? nullptr : subtitles.back().c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }

  fui::ListItem add;
  add.label = tr(STR_ADD_DICTIONARY);
  add.value = "+";
  add.actionValue = static_cast<int16_t>(dictionaries.size());
  rowItems.push_back(add);
}

void ManageDictionariesActivity::reload(const int preferred, const char* focusName) {
  {
    // The render task reads the rows, which point into `dictionaries`.
    RenderLock lock(*this);
    DictionaryRegistry::discover(dictionaries);
    rebuildRows();
    const int count = listCount();
    int target = std::clamp(preferred, 0, std::max(count - 1, 0));
    if (focusName) {
      for (size_t i = 0; i < dictionaries.size(); ++i) {
        if (dictionaries[i].name == focusName) {
          target = static_cast<int>(i);
          break;
        }
      }
    }
    nav.requestSelection(target);
  }
  requestUpdate();
}

void ManageDictionariesActivity::showToast(const StrId message) {
  toastText = I18N.get(message);
  toastTime = millis();
  requestUpdate();
}

void ManageDictionariesActivity::activateIndex(const int index) {
  if (actionsPopup.isActive() || index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  if (onDictionaryRow(index)) {
    useDictionary(index);
  } else {
    openPortal();
  }
}

bool ManageDictionariesActivity::handleCustomInput() {
  if (toastText && millis() - toastTime >= TOAST_MS) {
    toastText = nullptr;
    requestUpdate();
  }
  return actionsPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool ManageDictionariesActivity::handleButtons() {
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, ACTIONS_HOLD_MS)) {
    if (onDictionaryRow(nav.selected)) showActions(nav.selected);
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (dictionaries.empty()) {
      openPortal();
    } else {
      activateIndex(nav.selected);
    }
    return true;
  }
  return false;
}

void ManageDictionariesActivity::useDictionary(const int index) {
  if (!onDictionaryRow(index)) return;
  const std::string& name = dictionaries[index].name;
  if (name.size() > MAX_NAME_LENGTH) {
    // Too long for the settings field; renaming shortens it.
    LOG_ERR("MDICT", "Name too long to store: %s", name.c_str());
    showToast(StrId::STR_DICT_NAME_INVALID);
    return;
  }
  if (!isSelected(name)) {
    storeSelection(name.c_str());
    RenderLock lock(*this);
    rebuildRows();
  }
  requestUpdate();
}

void ManageDictionariesActivity::showActions(const int index) {
  if (!onDictionaryRow(index) || actionsPopup.isActive()) return;
  const char* options[ACTION_COUNT];
  for (int i = 0; i < ACTION_COUNT; i++) options[i] = I18N.get(ACTION_LABELS[i]);
  actionsPopup.show(tr(STR_DICTIONARY), dictionaries[index].name.c_str(), options, ACTION_COUNT, 0,
                    [this, index](const int action) {
                      switch (action) {
                        case ACTION_USE:
                          useDictionary(index);
                          return;
                        case ACTION_RENAME:
                          promptRename(index);
                          return;
                        case ACTION_DELETE:
                          promptDelete(index);
                          return;
                        case ACTION_ADD_MORE:
                          openPortal();
                          return;
                        default:
                          return;
                      }
                    });
  requestUpdate();
}

void ManageDictionariesActivity::promptRename(const int index) {
  if (!onDictionaryRow(index)) return;
  auto entry = makeUniqueNoThrow<TextEntryActivity>(renderer, mappedInput, tr(STR_RENAME), dictionaries[index].name,
                                                    MAX_NAME_LENGTH, InputType::Text);
  if (!entry) {
    LOG_ERR("MDICT", "OOM: rename text entry");
    return;
  }
  startActivityForResult(std::move(entry), [this, index](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
    if (keyboard) applyRename(index, keyboard->text);
  });
}

void ManageDictionariesActivity::applyRename(const int index, const std::string& text) {
  if (!onDictionaryRow(index)) return;
  const std::string newName = trimmed(text);
  if (newName.empty()) {
    showToast(StrId::STR_DICT_NAME_INVALID);
    return;
  }

  // Copied: reload() below rebuilds the entries this one lives in.
  const std::string oldName = dictionaries[index].name;
  const auto result =
      DictionaryRegistry::renameDictionary(oldName.c_str(), newName.c_str(), dictionaries[index].root, MAX_NAME_LENGTH);
  switch (result) {
    case DictionaryRegistry::RenameResult::Ok:
      break;
    case DictionaryRegistry::RenameResult::InvalidName:
      showToast(StrId::STR_DICT_NAME_INVALID);
      return;
    case DictionaryRegistry::RenameResult::NameTaken:
      showToast(StrId::STR_DICT_NAME_TAKEN);
      return;
    case DictionaryRegistry::RenameResult::Failed:
      showToast(StrId::STR_DICT_RENAME_FAILED);
      return;
  }

  if (isSelected(oldName)) storeSelection(newName.c_str());
  reload(index, newName.c_str());
}

void ManageDictionariesActivity::promptDelete(const int index) {
  if (!onDictionaryRow(index)) return;
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(
      renderer, mappedInput, tr(STR_DELETE) + std::string("? "), dictionaries[index].name);
  if (!confirmation) {
    LOG_ERR("MDICT", "OOM: delete confirmation");
    return;
  }
  startActivityForResult(std::move(confirmation), [this, index](const ActivityResult& result) {
    if (!result.isCancelled) applyDelete(index);
  });
}

void ManageDictionariesActivity::applyDelete(const int index) {
  if (!onDictionaryRow(index)) return;
  const std::string name = dictionaries[index].name;
  if (!DictionaryRegistry::deleteDictionary(name.c_str(), dictionaries[index].root)) {
    LOG_ERR("MDICT", "Delete failed: %s", name.c_str());
    showToast(StrId::STR_DICT_DELETE_FAILED);
    reload(index);  // a partial delete may have changed the list
    return;
  }
  // The setting stores only the name: keep it while a same-named copy in the
  // other root still resolves.
  std::string remaining;
  if (isSelected(name) && !DictionaryRegistry::resolveBasePath(name.c_str(), remaining)) {
    SETTINGS.dictionaryName[0] = '\0';
    SETTINGS.saveToFile();
  }
  reload(index);
}

void ManageDictionariesActivity::openPortal() {
  auto portal = makeUniqueNoThrow<DashboardPortalActivity>(renderer, mappedInput,
                                                           DashboardPortalActivity::Section::Dictionaries);
  if (!portal) {
    LOG_ERR("MDICT", "OOM: dictionaries portal");
    return;
  }
  // Leaving after the radio came on restarts the device into Settings, so this
  // result only arrives when the user backed out before that.
  startActivityForResult(std::move(portal), [this](const ActivityResult&) { reload(nav.selected); });
}

void ManageDictionariesActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (dictionaries.empty()) {
    fui::TextStyle heading = screen.theme().bodyText;
    heading.align = fui::TextAlign::Center;
    heading.bold = true;
    fui::TextStyle note = screen.theme().smallText;
    note.align = fui::TextAlign::Center;
    note.maxLines = 3;
    auto& target = screen.target();
    const int16_t headingHeight = target.lineHeight(heading.font);
    const int16_t noteHeight = static_cast<int16_t>(target.lineHeight(note.font) * note.maxLines);
    const int16_t gap = screen.theme().spaceMd;
    const int16_t blockHeight = static_cast<int16_t>(headingHeight + gap + noteHeight);
    const fui::Insets sides{0, static_cast<int16_t>(metrics.contentSidePadding), 0,
                            static_cast<int16_t>(metrics.contentSidePadding)};
    const fui::Rect body = screen.body();
    if (body.height > blockHeight) screen.spacer(static_cast<int16_t>((body.height - blockHeight) / 2));
    target.text(screen.takeTop(headingHeight, gap).inset(sides), tr(STR_NO_DICTIONARIES), heading);
    target.text(screen.takeTop(noteHeight).inset(sides), tr(STR_NO_DICTIONARIES_HINT), note);
    return;
  }

  // This hint names a physical button, so omit it on touch boards.
  if (!mappedInput.hasTouch()) {
    const int helpLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect band = screen.takeBottom(static_cast<int16_t>(helpLineHeight + metrics.verticalSpacing));
    GUI.drawHelpText(renderer, Rect{band.x, band.y + metrics.verticalSpacing, band.width, helpLineHeight},
                     tr(STR_HOLD_SELECT_FOR_ACTIONS));
  }

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

void ManageDictionariesActivity::drawFooter() {
  const char* select = onDictionaryRow(nav.selected) ? tr(STR_USE) : tr(STR_ADD);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), select, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void ManageDictionariesActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  for (int pass = 0; nav.consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }
  if (actionsPopup.processRender(renderer, mappedInput)) return;
  drawFooter();
  if (toastText) {
    // drawPopup overlays the framebuffer and refreshes the display itself.
    GUI.drawPopup(renderer, toastText);
    return;
  }
  renderer.displayBuffer();
}
