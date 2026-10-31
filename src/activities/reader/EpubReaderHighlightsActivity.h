#pragma once
#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "util/HighlightStore.h"

// Lists the book's highlights. Confirm jumps to one (a ProgressChangeResult
// carrying the spine and visible-text offset, as the bookmark list does);
// holding Confirm deletes it. Edits go straight to the reader's HighlightStore.
class EpubReaderHighlightsActivity final : public UiListActivity {
 public:
  explicit EpubReaderHighlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        const std::shared_ptr<Epub>& epub, HighlightStore& store);
  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override { return static_cast<int>(store.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;

  void rebuildRows();
  void openSelected();
  void deleteSelected();

  std::shared_ptr<Epub> epub;
  HighlightStore& store;
  // Row buffers derived from the store, rebuilt only when it changes.
  std::vector<std::string> subtitles;
  std::vector<freeink::ui::ListItem> rows;
  bool showToast = false;
  unsigned long toastTime = 0;
};
