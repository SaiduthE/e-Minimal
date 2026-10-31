#pragma once

#include <Epub/Page.h>
#include <EpdFontFamily.h>
#include <I18n.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class GfxRenderer;
class MappedInputManager;

// Two-stage, tap-only word picker over one laid-out reader page. Line stage
// highlights a whole text row (Up/Down change row, Confirm descends); Word
// stage highlights one word in that row (Up/Down change word, Confirm picks,
// Back returns to the Line stage). The owner activity forwards input via
// handleInput() and paints with drawCursorDelta()/drawCursor()/drawHints().
//
// Words keep the PageLine index and word index inside its TextBlock so owners
// that need text offsets (highlighting) can map a pick back to the page.
class PageTextPicker {
 public:
  enum class Stage : uint8_t { Line, Word };
  enum class Event : uint8_t { None, Moved, Picked, Back };

  // Screen box of one selectable word. `text` points into the page's TextBlock
  // arena (NUL-terminated), valid while the Page is alive.
  struct Word {
    int16_t x;
    int16_t y;      // top of the line box (highlight box origin)
    int16_t textY;  // drawText y: y shifted for super/subscript like TextBlock::render
    int16_t width;
    uint16_t row;         // selectable-row index (rows without a stop are not counted)
    uint16_t lineIndex;   // index of the PageLine in page.elements
    uint16_t wordInLine;  // word index inside that line's TextBlock
    const char* text;
    EpdFontFamily::Style style;
    int8_t tracking;  // block character spacing
  };

  // Extracts the selectable words of `page` and starts on the middle row in
  // Line stage. skipShortWords drops tokens with fewer than 3 letters/digits.
  // Returns false when the page has no selectable word.
  bool build(GfxRenderer& renderer, const Page& page, int fontId, int marginLeft, int marginTop,
             bool skipShortWords);

  // Allocates the differential-repaint buffer (call from onEnter, after
  // build). A failed allocation only disables the fast path.
  void allocSnapshot(GfxRenderer& renderer);
  void freeSnapshot() {
    snapshot.reset();
    snapshotCapacity = 0;
    snapshotValid = false;
  }

  // Reads Up/Down/Confirm/Back through the logical buttons.
  Event handleInput(const MappedInputManager& mappedInput);

  Stage stage() const { return currentStage; }
  void setStage(Stage s);
  // Jumps straight to `index` in Word stage (touch, Left/Right boards,
  // returning from a sub-activity).
  void selectWord(int index);
  int selectedWord() const { return wordIndex; }
  int pickedWord() const { return wordIndex; }
  int selectedRow() const { return rowIndex; }

  int wordCount() const { return static_cast<int>(words.size()); }
  int rowCount() const { return static_cast<int>(rowStarts.size()) - 1; }
  const Word& word(int index) const { return words[index]; }
  // Word range [rowBegin(r), rowEnd(r)) of a row.
  int rowBegin(int row) const { return rowStarts[row]; }
  int rowEnd(int row) const { return rowStarts[row + 1]; }

  int closestInRow(int row, int centerX) const;
  int wordAt(int x, int y) const;  // finger-sized slop; -1 when none

  // Paints the cursor for the current stage and saves the pixels under it.
  // Returns false when they could not be saved (next move needs a full repaint).
  bool drawCursor(GfxRenderer& renderer);
  // Differential repaint: restores the pixels under the previously drawn
  // cursor and draws the current one. Returns false (nothing drawn) when the
  // framebuffer is not known to hold a clean page; the owner then repaints
  // the page and calls drawCursor().
  bool drawCursorDelta(GfxRenderer& renderer);
  // The framebuffer no longer matches the saved pixels (popup, sub-activity).
  void invalidateSnapshot() { snapshotValid = false; }

  // Button hints for the current stage; `wordConfirm` labels Confirm in Word stage.
  void drawHints(GfxRenderer& renderer, const MappedInputManager& mappedInput, StrId wordConfirm) const;

 private:
  struct Box {
    int x, y, w, h;
  };

  int cursorPos() const { return currentStage == Stage::Line ? rowIndex : wordIndex; }
  // Word range [begin, end) the cursor covers in the current stage.
  void cursorRange(int& begin, int& end) const;
  Box boxFor(int begin, int end) const;  // clamped to the panel
  void enterWordStage();
  void prewarmCursorGlyphs(GfxRenderer& renderer) const;

  std::vector<Word> words;
  std::vector<uint16_t> rowStarts;  // rowCount()+1 entries; last is words.size()
  int fontId = 0;
  int lineHeight = 0;
  int screenWidth = 0;

  Stage currentStage = Stage::Line;
  int rowIndex = 0;
  int wordIndex = 0;

  // Pixels under the drawn cursor box. snapshotValid: they match the box
  // recorded in snapshotBox and the framebuffer holds a clean page.
  std::unique_ptr<uint8_t[]> snapshot;
  size_t snapshotCapacity = 0;
  Box snapshotBox{0, 0, 0, 0};
  bool snapshotValid = false;
};
