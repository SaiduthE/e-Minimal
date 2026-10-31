#pragma once

#include <I18n.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "util/DictionaryRegistry.h"

// Settings > Reader > Manage Dictionaries (five-button boards). Lists the
// dictionaries on the card plus an "Add Dictionary" row that opens the web
// Dictionaries page by QR. Select makes a dictionary the default; hold Select
// opens Use / Rename / Delete / Add More. With none installed it explains how
// to add some and Select opens the same page.
class ManageDictionariesActivity final : public UiListActivity {
 public:
  ManageDictionariesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override { return dictionaries.empty() ? 0 : static_cast<int>(dictionaries.size()) + 1; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  bool onDictionaryRow(int index) const { return index >= 0 && index < static_cast<int>(dictionaries.size()); }

  // Re-scan the card and rebuild the rows. The cursor goes to `focusName` when
  // it is still listed, else stays near `preferred`.
  void reload(int preferred, const char* focusName = nullptr);
  void rebuildRows();
  void showToast(StrId message);

  void useDictionary(int index);
  void showActions(int index);
  void promptRename(int index);
  void applyRename(int index, const std::string& text);
  void promptDelete(int index);
  void applyDelete(int index);
  void openPortal();

  std::vector<DictionaryEntry> dictionaries;
  // Row text and items derived from `dictionaries`; the items point into both.
  // Rebuilt only when the list or the selection changes.
  std::vector<std::string> subtitles;
  std::vector<freeink::ui::ListItem> rowItems;

  OptionPopup actionsPopup;
  const char* toastText = nullptr;
  unsigned long toastTime = 0;
};
