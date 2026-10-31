#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include <vector>

#include "activities/UiListActivity.h"
#include "util/DictionaryRegistry.h"

class MappedInputManager;

// Simple list of the dictionaries on the card. Confirm stores the pick in
// SETTINGS.dictionaryName (and saves) then finishes; Back finishes without
// changing anything, so callers check SETTINGS.dictionaryName afterwards.
class DictionaryChooserActivity final : public UiListActivity {
 public:
  DictionaryChooserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                            std::vector<DictionaryEntry>&& dictionaries);

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(dictionaries.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  std::vector<DictionaryEntry> dictionaries;
  std::vector<freeink::ui::ListItem> rowItems;  // built once in onEnter(), reused every repaint
};
