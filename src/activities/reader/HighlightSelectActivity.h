#pragma once

#include <Epub/Page.h>
#include <I18n.h>

#include <memory>

#include "PageTextPicker.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/HighlightStore.h"

// Marks a text range on the current reader page with the two-stage tap picker
// (PageTextPicker): pick the first word (line, then word), then the last word.
// While the end is being chosen the words between the two picks are shown with
// the highlight band. Finishes with a HighlightResult (new range, or the index
// of an existing highlight to remove) or as cancelled.
class HighlightSelectActivity final : public Activity {
 public:
  explicit HighlightSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Page> page,
                                   int marginLeft, int marginTop, uint16_t spineIndex, const HighlightStore& store)
      : Activity("HighlightSelect", renderer, mappedInput),
        page(std::move(page)),
        marginLeft(marginLeft),
        marginTop(marginTop),
        spineIndex(spineIndex),
        store(store) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Step : uint8_t { Start, End };

  void wordRange(int index, uint32_t& start, uint32_t& end) const;
  void onStartPicked();
  void beginEndStep();
  void finishWithRange(int first, int last);
  void finishCancelled();
  // Word indices [first, last] shown as the live range in the End step.
  void previewRange(int& first, int& last) const;
  void paintPreview(int first, int last) const;
  void drawHints() const;

  std::unique_ptr<Page> page;
  const int marginLeft;
  const int marginTop;
  const uint16_t spineIndex;
  const HighlightStore& store;
  int fontId = 0;

  PageTextPicker picker;
  bool cursorMoved = false;  // set by our own cursor moves; gates the differential repaint
  Step step = Step::Start;
  int startWord = -1;
  OptionPopup popup;
};
