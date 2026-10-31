#include "HighlightSelectActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <variant>

#include "CrossPointSettings.h"
#include "components/UITheme.h"

static_assert(sizeof(HighlightResult::snippet) == HighlightStore::SNIPPET_BYTES,
              "HighlightResult::snippet must match HighlightStore::SNIPPET_BYTES");

void HighlightSelectActivity::onEnter() {
  Activity::onEnter();
  fontId = SETTINGS.getReaderFontId();
  picker.build(renderer, *page, fontId, marginLeft, marginTop, /*skipShortWords=*/false);
  picker.allocSnapshot(renderer);
  requestUpdate();
}

void HighlightSelectActivity::onExit() {
  picker.freeSnapshot();
  Activity::onExit();
}

// Visible-text range of a picker word, read from its line's TextBlock.
void HighlightSelectActivity::wordRange(const int index, uint32_t& start, uint32_t& end) const {
  const PageTextPicker::Word& w = picker.word(index);
  const auto* line = static_cast<const PageLine*>(page->elements[w.lineIndex].get());
  start = line->getBlock()->wordVisibleOffset(w.wordInLine);
  end = line->getBlock()->wordVisibleEnd(w.wordInLine);
}

void HighlightSelectActivity::beginEndStep() {
  startWord = picker.selectedWord();
  step = Step::End;
  picker.setStage(PageTextPicker::Stage::Line);  // back on the start word's row
  picker.invalidateSnapshot();                   // the preview needs full repaints
  requestUpdate();
}

void HighlightSelectActivity::onStartPicked() {
  uint32_t start = 0;
  uint32_t end = 0;
  wordRange(picker.selectedWord(), start, end);
  const int existing = end > start ? store.findOverlap(spineIndex, start, end) : -1;
  if (existing < 0) {
    beginEndStep();
    return;
  }
  const StrId options[] = {StrId::STR_REMOVE_HIGHLIGHT, StrId::STR_HIGHLIGHT};
  picker.invalidateSnapshot();  // the popup overdraws the page
  popup.show(StrId::STR_HIGHLIGHTS, options, 2, 0, [this, existing](const int choice) {
    if (choice == 0) {
      HighlightResult result;
      result.remove = true;
      result.removeIndex = existing;
      setResult(std::move(result));
      finish();
    } else {
      beginEndStep();
    }
  });
  requestUpdate();
}

void HighlightSelectActivity::finishCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

// Builds the range from the words first..last (reading order).
void HighlightSelectActivity::finishWithRange(const int first, const int last) {
  HighlightResult result;
  result.spine = spineIndex;
  result.start = UINT32_MAX;
  result.end = 0;
  result.snippet[0] = '\0';
  bool any = false;
  for (int i = first; i <= last; i++) {
    uint32_t s = 0;
    uint32_t e = 0;
    wordRange(i, s, e);
    if (e <= s) continue;  // zero-length (unknown-offset) words would corrupt the range
    any = true;
    result.start = std::min(result.start, s);
    result.end = std::max(result.end, e);
    HighlightStore::appendSnippetWord(result.snippet, picker.word(i).text);
  }
  if (!any) {
    finishCancelled();
    return;
  }
  setResult(std::move(result));
  finish();
}

void HighlightSelectActivity::previewRange(int& first, int& last) const {
  int cursor = startWord;
  if (picker.stage() == PageTextPicker::Stage::Word) {
    cursor = picker.selectedWord();
  } else {
    // Line stage: a later row extends to its last word, an earlier one back to its first.
    const int startRow = picker.word(startWord).row;
    const int row = picker.selectedRow();
    if (row > startRow) {
      cursor = picker.rowEnd(row) - 1;
    } else if (row < startRow) {
      cursor = picker.rowBegin(row);
    }
  }
  first = std::min(startWord, cursor);
  last = std::max(startWord, cursor);
}

void HighlightSelectActivity::loop() {
  if (popup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  if (picker.wordCount() == 0) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) finishCancelled();
    return;
  }

  switch (picker.handleInput(mappedInput)) {
    case PageTextPicker::Event::Back:
      if (step == Step::End) {
        step = Step::Start;
        startWord = -1;
        picker.invalidateSnapshot();
        requestUpdate();
      } else {
        finishCancelled();
      }
      break;
    case PageTextPicker::Event::Picked:
      if (step == Step::Start) {
        onStartPicked();
      } else {
        int first = 0;
        int last = 0;
        previewRange(first, last);
        finishWithRange(first, last);
      }
      break;
    case PageTextPicker::Event::Moved:
      cursorMoved = true;
      requestUpdate();
      break;
    case PageTextPicker::Event::None:
      break;
  }
}

void HighlightSelectActivity::drawHints() const {
  picker.drawHints(renderer, mappedInput, step == Step::Start ? StrId::STR_HL_START_HERE : StrId::STR_HL_END_HERE);
}

// One band per row of the preview range.
void HighlightSelectActivity::paintPreview(const int first, const int last) const {
  const int lineHeight = renderer.getLineHeight(fontId);
  int runStart = first;
  for (int i = first; i <= last; i++) {
    if (i < last && picker.word(i + 1).row == picker.word(i).row) continue;
    const PageTextPicker::Word& a = picker.word(runStart);
    const PageTextPicker::Word& b = picker.word(i);
    HighlightStore::paintBand(renderer, a.x, a.y, b.x + b.width - a.x, lineHeight);
    runStart = i + 1;
  }
}

void HighlightSelectActivity::render(RenderLock&&) {
  // Step A moves only the cursor: restore the pixels under the old one and
  // draw the new one. Step B repaints the page (the preview band changes).
  // Only our own cursor moves qualify; any other render (an overlay closing)
  // finds an unrelated framebuffer and repaints fully.
  const bool moved = cursorMoved;
  cursorMoved = false;
  if (moved && step == Step::Start && !popup.isActive() && picker.drawCursorDelta(renderer)) {
    drawHints();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  renderer.clearScreen();

  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  page->render(renderer, fontId, marginLeft, marginTop);
  scope.endScanAndPrewarm();

  // Bands go under the text; the dither is the only grey a 1bpp frame has.
  size_t count = 0;
  const HighlightStore::Highlight* ranges = store.spineRange(spineIndex, count);
  HighlightStore::paintPage(renderer, *page, fontId, marginLeft, marginTop, ranges, count);
  if (step == Step::End && picker.wordCount() > 0) {
    int first = 0;
    int last = 0;
    previewRange(first, last);
    paintPreview(first, last);
  }
  page->render(renderer, fontId, marginLeft, marginTop);

  picker.drawCursor(renderer);
  drawHints();

  if (popup.isActive()) {
    picker.invalidateSnapshot();
    popup.processRender(renderer, mappedInput);  // draws over the page and refreshes
    return;
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
