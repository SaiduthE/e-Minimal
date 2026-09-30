#pragma once

#include <vector>

#include "IdleStandby.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// The To-do tile opened full screen: today's date and the to-do list
// (TodoStore). The first row adds a task; Select on a task ticks it, holding
// Select offers edit / delete / clear done. Back returns to the tiles.
class TodoDashboardActivity final : public UiListActivity {
 public:
  explicit TodoDashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 bool cleanInitialRefresh = false);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // The dashboard's standby (IdleStandby) instead of the deep auto-sleep.
  bool preventAutoSleep() override { return true; }

 private:
  static constexpr int ADD_ROW = 0;
  static constexpr int FIRST_TASK_ROW = 1;
  static constexpr int SUMMARY_LENGTH = 48;
  static constexpr int DATE_LENGTH = 40;

  // The add row plus one row per task.
  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void onBackButton() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  // Runs edit() (a TodoStore mutator returning whether anything changed) under
  // the render lock with the row rebuild, then saves outside it.
  template <typename Edit>
  void editList(Edit&& edit);
  void rebuildRows();
  void refreshSummary();
  // Formats today's date into `date` (empty without a clock). Runs per build,
  // so a dashboard left open shows the new day on its next paint.
  void refreshDate();
  void promptForTask(int taskIndex);
  void showTaskActions(int taskIndex);
  static int taskIndexForRow(int row) { return row - FIRST_TASK_ROW; }

  const bool cleanInitialRefresh;
  bool firstRenderDone = false;
  OptionPopup actionsPopup;
  IdleStandby standby;
  // Rows point into TodoStore's strings: rebuilt on every list change, before
  // the render that reads them.
  std::vector<freeink::ui::ListItem> rowItems;
  char summary[SUMMARY_LENGTH]{};
  char date[DATE_LENGTH]{};
};
