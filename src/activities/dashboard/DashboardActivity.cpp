#include "DashboardActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <ctime>

#include "MappedInputManager.h"
#include "TodoStore.h"
#include "activities/util/TextEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long ACTIONS_HOLD_MS = 700;

enum TaskAction { TASK_TOGGLE, TASK_EDIT, TASK_DELETE, TASK_CLEAR_DONE, TASK_ACTION_COUNT };
constexpr StrId TASK_ACTIONS[TASK_ACTION_COUNT] = {StrId::STR_TODO_TOGGLE, StrId::STR_TODO_EDIT, StrId::STR_DELETE,
                                                   StrId::STR_TODO_CLEAR_DONE};
}  // namespace

DashboardActivity::DashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const bool cleanInitialRefresh)
    : UiListActivity("Dashboard", renderer, mappedInput), cleanInitialRefresh(cleanInitialRefresh) {}

void DashboardActivity::onEnter() {
  {
    RenderLock lock(*this);
    TODO_STORE.load();
    // One allocation for the whole screen's life: ~50 ListItems, no regrowth as tasks are added.
    rowItems.reserve(TodoStore::MAX_ITEMS + FIRST_TASK_ROW);
    rebuildRows();
  }
  UiListActivity::onEnter();
}

void DashboardActivity::onExit() {
  UiListActivity::onExit();
  rowItems.clear();
  rowItems.shrink_to_fit();
  TODO_STORE.unload();
}

const char* DashboardActivity::headerTitle() const { return tr(STR_APP_DASHBOARD); }

void DashboardActivity::rebuildRows() {
  rowItems.clear();
  fui::ListItem add;
  add.label = TODO_STORE.isFull() ? tr(STR_TODO_FULL) : tr(STR_TODO_ADD);
  add.value = "+";
  add.actionValue = ADD_ROW;
  add.enabled = !TODO_STORE.isFull();
  add.sectionHeading = tr(STR_TODO);
  rowItems.push_back(add);

  const auto& items = TODO_STORE.getItems();
  for (size_t i = 0; i < items.size(); i++) {
    fui::ListItem row;
    row.label = items[i].text.c_str();
    row.toggle = true;
    row.toggleChecked = items[i].done;
    row.actionValue = static_cast<int16_t>(FIRST_TASK_ROW + i);
    rowItems.push_back(row);
  }
  refreshSummary();
}

void DashboardActivity::refreshSummary() {
  const size_t done = TODO_STORE.doneCount();
  const size_t open = TODO_STORE.count() - done;
  if (TODO_STORE.count() == 0) {
    snprintf(summary, sizeof(summary), "%s", tr(STR_TODO_EMPTY));
  } else {
    snprintf(summary, sizeof(summary), tr(STR_TODO_SUMMARY_FORMAT), static_cast<unsigned>(open),
             static_cast<unsigned>(done));
  }
}

void DashboardActivity::refreshDate() {
  date[0] = '\0';
  struct tm now{};
  if (!halClock.isAvailable() || !halClock.localTime(now)) return;
  char weekday[16];
  char month[16];
  strftime(weekday, sizeof(weekday), "%A", &now);
  strftime(month, sizeof(month), "%B", &now);
  snprintf(date, sizeof(date), "%s, %d %s %d", weekday, now.tm_mday, month, now.tm_year + 1900);
}

template <typename Edit>
void DashboardActivity::editList(Edit&& edit) {
  bool changed;
  {
    // The render task reads rowItems, whose labels point into the store.
    RenderLock lock(*this);
    changed = edit();
    if (changed) {
      rebuildRows();
      if (nav.selected >= listCount()) nav.requestSelection(listCount() - 1);
    }
  }
  if (changed && !TODO_STORE.saveToFile()) LOG_ERR("DASH", "Failed to save the to-do list");
  requestUpdate();
}

void DashboardActivity::activateIndex(const int index) {
  if (actionsPopup.isActive()) return;
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;

  if (index == ADD_ROW) {
    promptForTask(-1);
    return;
  }
  const auto task = static_cast<size_t>(taskIndexForRow(index));
  editList([task] { return TODO_STORE.toggle(task); });
}

void DashboardActivity::onRowLongPress(const int index) {
  if (actionsPopup.isActive() || index < FIRST_TASK_ROW || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  showTaskActions(taskIndexForRow(index));
}

bool DashboardActivity::handleCustomInput() {
  return actionsPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool DashboardActivity::handleButtons() {
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, ACTIONS_HOLD_MS)) {
    const int selected = nav.selected;
    if (selected >= FIRST_TASK_ROW && selected < listCount()) showTaskActions(taskIndexForRow(selected));
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }
  // Back is swallowed: the dashboard is an app root.
  return mappedInput.wasReleased(MappedInputManager::Button::Back);
}

void DashboardActivity::promptForTask(const int taskIndex) {
  const bool editing = taskIndex >= 0;
  if (!editing && TODO_STORE.isFull()) {
    LOG_DBG("DASH", "To-do list full");
    return;
  }
  const std::string initial = editing ? TODO_STORE.getItems()[taskIndex].text : std::string();
  auto entry =
      makeUniqueNoThrow<TextEntryActivity>(renderer, mappedInput, editing ? tr(STR_TODO_EDIT) : tr(STR_TODO_NEW),
                                           initial, TodoStore::MAX_TEXT_LENGTH, InputType::Text);
  if (!entry) {
    LOG_ERR("DASH", "OOM: to-do text entry");
    return;
  }
  startActivityForResult(std::move(entry), [this, taskIndex](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* keyboard = std::get_if<KeyboardResult>(&result.data);
    if (!keyboard || keyboard->text.empty()) return;
    const std::string& text = keyboard->text;
    if (taskIndex >= 0) {
      editList([taskIndex, &text] { return TODO_STORE.setText(static_cast<size_t>(taskIndex), text); });
      return;
    }
    // Selection stays on the add row, so a run of adds is Select, type, Select.
    editList([&text] { return TODO_STORE.add(text); });
  });
}

void DashboardActivity::showTaskActions(const int taskIndex) {
  if (taskIndex < 0 || taskIndex >= static_cast<int>(TODO_STORE.count())) return;
  const char* options[TASK_ACTION_COUNT];
  for (int i = 0; i < TASK_ACTION_COUNT; i++) options[i] = I18N.get(TASK_ACTIONS[i]);
  actionsPopup.show(tr(STR_TODO), TODO_STORE.getItems()[taskIndex].text.c_str(), options, TASK_ACTION_COUNT, 0,
                    [this, taskIndex](const int action) {
                      const auto index = static_cast<size_t>(taskIndex);
                      switch (action) {
                        case TASK_TOGGLE:
                          editList([index] { return TODO_STORE.toggle(index); });
                          return;
                        case TASK_EDIT:
                          promptForTask(taskIndex);
                          return;
                        case TASK_DELETE:
                          editList([index] { return TODO_STORE.remove(index); });
                          return;
                        case TASK_CLEAR_DONE:
                          editList([] { return TODO_STORE.removeDone(); });
                          return;
                        default:
                          return;
                      }
                    });
  requestUpdate();
}

void DashboardActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  const int16_t inset = static_cast<int16_t>(metrics.contentSidePadding);
  auto& target = screen.target();
  refreshDate();
  if (date[0] != '\0') {
    auto dateText = theme.titleText;
    dateText.bold = true;
    const fui::Rect rect = screen.takeTop(target.lineHeight(dateText.font), theme.spaceSm);
    target.text(fui::Rect{static_cast<int16_t>(rect.x + inset), rect.y, static_cast<int16_t>(rect.width - inset * 2),
                          rect.height},
                date, dateText);
  }
  const fui::Rect summaryRect =
      screen.takeTop(target.lineHeight(theme.bodyText.font), static_cast<int16_t>(metrics.verticalSpacing));
  target.text(fui::Rect{static_cast<int16_t>(summaryRect.x + inset), summaryRect.y,
                        static_cast<int16_t>(summaryRect.width - inset * 2), summaryRect.height},
              summary, theme.bodyText);

  // This hint names a physical button, so omit it on touch boards.
  if (!mappedInput.hasTouch() && TODO_STORE.count() > 0) {
    const int helpLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect band = screen.takeBottom(static_cast<int16_t>(helpLineHeight + metrics.verticalSpacing));
    GUI.drawHelpText(renderer, Rect{band.x, band.y + metrics.verticalSpacing, band.width, helpLineHeight},
                     tr(STR_TODO_HINT));
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  syncListViewport(screen, props);
  screen.list(props);
}

void DashboardActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void DashboardActivity::render(RenderLock&&) {
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
  // After a wake the panel still shows the sleep image: clear it once.
  renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  firstRenderDone = true;
}
