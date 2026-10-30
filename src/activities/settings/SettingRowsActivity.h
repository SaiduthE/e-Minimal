#pragma once

#include <I18n.h>

#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "SettingsActivity.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// A settings sub-page: a short fixed list of toggle/enum rows taken from the
// shared SettingsList entries (same labels and values as the web API). Two-way
// rows flip on Select; longer enums open a popup. Every change is saved.
class SettingRowsActivity : public UiListActivity {
 public:
  using Field = uint8_t CrossPointSettings::*;
  static constexpr int MAX_ROWS = 6;

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;

 protected:
  SettingRowsActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput, StrId title,
                      const Field* fields, int fieldCount);

  // Called after a row's value changed, before it is saved.
  virtual void onSettingChanged(Field field) { (void)field; }

 private:
  const StrId title;
  const Field* const fields;
  const int fieldCount;

  OptionPopup optionPopup;
  // Filled in onEnter() from getSettingsList(), released in onExit().
  std::vector<SettingInfo> infos;
  std::string rowValues[MAX_ROWS];
  freeink::ui::ListItem rowItems[MAX_ROWS]{};

  int listCount() const override { return static_cast<int>(infos.size()); }
  const char* headerTitle() const override { return I18N.get(title); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;

  void applyValue(const SettingInfo& info, uint8_t value);
};
