#pragma once

#include <cstdint>

#include "DashboardConfigStore.h"
#include "activities/UiListActivity.h"

namespace dashboard_widgets {

constexpr uint8_t COUNT = 4;

// Whether each widget has something to show: a weather place, tasks on the
// to-do list, images in the image folder, a set clock for the date.
struct Status {
  bool hasPlace = false;
  uint16_t todoOpen = 0;
  uint16_t todoTotal = 0;
  uint16_t images = 0;
  bool clockSet = false;

  uint8_t readyCount() const;
};

// Reads the loaded DashboardConfigStore, the to-do file and the image folder,
// counting at most maxImages images. Touches the card: loop task only.
Status readStatus(uint16_t maxImages);

const char* widgetName(WidgetId widget);

}  // namespace dashboard_widgets

// The Dashboard's widgets and whether each is set up and on the layout in use.
// Setting one up happens in the web interface: Select opens the dashboard
// portal on that widget's page.
class WidgetsActivity final : public UiListActivity {
 public:
  explicit WidgetsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("DashboardWidgets", renderer, mappedInput) {}

  void onEnter() override;

 private:
  // A folder path (up to DashboardConfigStore::MAX_FOLDER_LENGTH) and a count.
  static constexpr int VALUE_LENGTH = 176;

  int listCount() const override { return dashboard_widgets::COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Reads the status, then fills the rows under the render lock.
  void refreshRows();

  freeink::ui::ListItem rowItems_[dashboard_widgets::COUNT]{};
  char values_[dashboard_widgets::COUNT][VALUE_LENGTH]{};
};
