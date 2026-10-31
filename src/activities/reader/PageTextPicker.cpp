#include "PageTextPicker.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <climits>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace {

constexpr int CURSOR_PAD = 2;       // highlight box margin around the text
constexpr int WORD_TOUCH_SLOP = 4;  // matches the box padding plus finger error
constexpr int MIN_LETTERS = 3;      // skipShortWords threshold
// Panel-alignment slack of readFramebufferRegion: the box grows to whole bytes
// along the panel's x axis, so each dimension may gain up to 15 pixels.
constexpr int SNAPSHOT_ALIGN_SLACK = 16;

// Counts the letters/digits of a token: ASCII alphanumerics plus non-ASCII
// codepoints, except U+2000-U+206F (dashes, bullets and other General
// Punctuation that appear as standalone tokens are not words).
int letterCount(const char* text) {
  int count = 0;
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(text); *p != 0; p++) {
    if (*p < 0x80) {
      if (std::isalnum(*p)) count++;
    } else if (*p == 0xE2 && (p[1] == 0x80 || p[1] == 0x81)) {
      if (p[2] == 0) break;  // truncated sequence: skipping would step past the NUL
      p += 2;                // skip the 3-byte General Punctuation codepoint
    } else if ((*p & 0xC0) != 0x80) {
      count++;  // lead byte of one codepoint; continuation bytes are not counted again
    }
  }
  return count;
}

uint8_t styleBit(const EpdFontFamily::Style style) {
  return static_cast<uint8_t>(1u << (static_cast<uint8_t>(style) & 0x03));
}

}  // namespace

bool PageTextPicker::build(GfxRenderer& renderer, const Page& page, const int fontIdIn, const int marginLeft,
                           const int marginTop, const bool skipShortWords) {
  fontId = fontIdIn;
  lineHeight = renderer.getLineHeight(fontId);
  screenWidth = renderer.getScreenWidth();
  words.clear();
  words.reserve(128);
  rowStarts.clear();
  rowStarts.push_back(0);
  snapshotValid = false;

  // Single walk: collect the selectable words while accumulating their text
  // and styles (~2KB transient string, freed on return). Widths are measured
  // afterwards: merging the page's codepoints into the SD font's persistent
  // advance table first keeps getTextAdvanceX on the in-RAM path instead of
  // loading glyphs from SD one overflow slot at a time.
  std::string pageText;
  pageText.reserve(2048);
  uint8_t styleMask = 0;
  const int minLetters = skipShortWords ? MIN_LETTERS : 1;
  const int ascender = renderer.getFontAscenderSize(fontId);

  for (size_t e = 0; e < page.elements.size(); e++) {
    if (page.elements[e]->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(page.elements[e].get());
    const auto* block = line->getBlock();
    if (!block || !block->valid()) continue;

    const size_t rowFirstWord = words.size();
    const int rubyShift = block->getRubyShift(ascender);
    const int8_t tracking = block->getBlockStyle().characterSpacing;
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      const char* text = block->wordText(i);
      if (letterCount(text) < minLetters) continue;

      Word w;
      w.x = static_cast<int16_t>(line->xPos + block->wordXpos(i) + marginLeft);
      w.y = static_cast<int16_t>(line->yPos + marginTop + rubyShift);
      // Same baseline shifts as TextBlock::render.
      int textY = w.y;
      const EpdFontFamily::Style wordStyle = block->wordStyle(i);
      if ((wordStyle & EpdFontFamily::SUP) != 0) {
        textY -= ascender * 2 / 5;
      } else if ((wordStyle & EpdFontFamily::SUB) != 0) {
        textY += ascender / 4;
      }
      w.textY = static_cast<int16_t>(textY);
      w.tracking = tracking;
      w.width = 0;  // measured below, once the advance table is ready
      w.row = static_cast<uint16_t>(rowStarts.size() - 1);
      w.lineIndex = static_cast<uint16_t>(e);
      w.wordInLine = i;
      w.text = text;
      w.style = wordStyle;
      words.push_back(w);

      pageText.append(text);
      pageText.push_back(' ');
      styleMask |= styleBit(w.style);
    }
    if (words.size() > rowFirstWord) rowStarts.push_back(static_cast<uint16_t>(words.size()));
  }

  if (styleMask == 0) styleMask = 0x01;  // REGULAR
  renderer.ensureSdCardFontReady(fontId, pageText.c_str(), styleMask);
  for (auto& w : words) {
    w.width = static_cast<int16_t>(renderer.getTextAdvanceX(fontId, w.text, w.style, w.tracking));
  }

  currentStage = Stage::Line;
  rowIndex = rowCount() / 2;
  wordIndex = words.empty() ? 0 : rowBegin(rowIndex);
  return !words.empty();
}

void PageTextPicker::allocSnapshot(GfxRenderer& renderer) {
  // A whole-line box spans the screen width: 1bpp, so (width + slack) / 8
  // bytes per row (+1 for the byte rounding) times the box height + slack.
  const int boxHeight = lineHeight + 2 * CURSOR_PAD + SNAPSHOT_ALIGN_SLACK;
  const int rowBytes = (renderer.getScreenWidth() + SNAPSHOT_ALIGN_SLACK) / 8 + 1;
  snapshotCapacity = static_cast<size_t>(rowBytes) * boxHeight;
  snapshot = makeUniqueNoThrow<uint8_t[]>(snapshotCapacity);
  if (!snapshot) {
    LOG_ERR("PTP", "Snapshot alloc failed (%u bytes); full repaints only", static_cast<unsigned>(snapshotCapacity));
    snapshotCapacity = 0;
  }
  snapshotValid = false;
}

void PageTextPicker::setStage(const Stage s) {
  if (words.empty()) return;
  if (s == Stage::Word) {
    enterWordStage();
  } else {
    rowIndex = words[wordIndex].row;
    currentStage = Stage::Line;
  }
}

void PageTextPicker::selectWord(const int index) {
  if (index < 0 || index >= wordCount()) return;
  wordIndex = index;
  rowIndex = words[index].row;
  currentStage = Stage::Word;
}

// Word stage starts on the longest word of the row (ties: leftmost).
void PageTextPicker::enterWordStage() {
  int best = rowBegin(rowIndex);
  size_t bestLen = 0;
  for (int i = rowBegin(rowIndex); i < rowEnd(rowIndex); i++) {
    const size_t len = std::strlen(words[i].text);
    if (len > bestLen) {
      bestLen = len;
      best = i;
    }
  }
  wordIndex = best;
  currentStage = Stage::Word;
}

PageTextPicker::Event PageTextPicker::handleInput(const MappedInputManager& mappedInput) {
  using Button = MappedInputManager::Button;
  if (words.empty()) {
    return mappedInput.wasReleased(Button::Back) ? Event::Back : Event::None;
  }

  if (mappedInput.wasReleased(Button::Back)) {
    if (currentStage == Stage::Line) return Event::Back;
    setStage(Stage::Line);
    return Event::Moved;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    if (currentStage == Stage::Line) {
      enterWordStage();
      return Event::Moved;
    }
    return Event::Picked;
  }

  int step = 0;
  if (mappedInput.wasReleased(Button::Up)) {
    step = -1;
  } else if (mappedInput.wasReleased(Button::Down)) {
    step = 1;
  }
  if (step == 0) return Event::None;

  if (currentStage == Stage::Line) {
    const int target = rowIndex + step;
    if (target < 0 || target >= rowCount()) return Event::None;
    rowIndex = target;
    return Event::Moved;
  }
  const int target = wordIndex + step;
  if (target < rowBegin(rowIndex) || target >= rowEnd(rowIndex)) return Event::None;
  wordIndex = target;
  return Event::Moved;
}

// Index of the word in `row` whose horizontal center is closest to centerX;
// -1 when the row is out of range.
int PageTextPicker::closestInRow(const int row, const int centerX) const {
  if (row < 0 || row >= rowCount()) return -1;
  int best = -1;
  int bestDistance = INT_MAX;
  for (int i = rowBegin(row); i < rowEnd(row); i++) {
    const int distance = std::abs(words[i].x + words[i].width / 2 - centerX);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = i;
    }
  }
  return best;
}

// Boxes never overlap after the slop grows them, at worst they touch, so
// first hit wins.
int PageTextPicker::wordAt(const int x, const int y) const {
  for (int i = 0; i < wordCount(); i++) {
    const Word& w = words[i];
    if (x >= w.x - WORD_TOUCH_SLOP && x < w.x + w.width + WORD_TOUCH_SLOP && y >= w.y - WORD_TOUCH_SLOP &&
        y < w.y + lineHeight + WORD_TOUCH_SLOP) {
      return i;
    }
  }
  return -1;
}

void PageTextPicker::cursorRange(int& begin, int& end) const {
  begin = currentStage == Stage::Line ? rowBegin(rowIndex) : wordIndex;
  end = currentStage == Stage::Line ? rowEnd(rowIndex) : wordIndex + 1;
}

// Highlight box of words [begin, end), clamped to the panel so save, draw and
// restore all use the same rectangle.
PageTextPicker::Box PageTextPicker::boxFor(const int begin, const int end) const {
  const Word& first = words[begin];
  const Word& last = words[end - 1];
  const int left = first.x;
  const int right = last.x + last.width;
  Box box{left - CURSOR_PAD, first.y - CURSOR_PAD, right - left + 2 * CURSOR_PAD, lineHeight + 2 * CURSOR_PAD};
  if (box.x < 0) {
    box.w += box.x;
    box.x = 0;
  }
  if (box.y < 0) {
    box.h += box.y;
    box.y = 0;
  }
  box.w = std::min(box.w, screenWidth - box.x);
  return box;
}

void PageTextPicker::prewarmCursorGlyphs(GfxRenderer& renderer) const {
  // The full path's PrewarmScope cleared the glyph cache on exit; batch-load
  // just the highlighted words' glyphs before drawing them white-on-black.
  auto* fcm = renderer.getFontCacheManager();
  int begin;
  int end;
  cursorRange(begin, end);
  for (int i = begin; i < end; i++) {
    fcm->prewarmCache(fontId, words[i].text, styleBit(words[i].style));
  }
}

bool PageTextPicker::drawCursor(GfxRenderer& renderer) {
  if (words.empty()) return false;
  int begin;
  int end;
  cursorRange(begin, end);
  const Box box = boxFor(begin, end);

  bool saved = false;
  if (snapshot && box.w > 0 && box.h > 0) {
    saved = renderer.readFramebufferRegion(box.x, box.y, box.w, box.h, snapshot.get(), snapshotCapacity) > 0;
  }
  snapshotBox = box;
  snapshotValid = saved;

  renderer.fillRect(box.x, box.y, box.w, box.h, true);
  // Focus-reading bold prefixes are not reproduced: the inverted word is drawn
  // in its plain style, which can differ by a pixel or two from the page.
  for (int i = begin; i < end; i++) {
    renderer.drawText(fontId, words[i].x, words[i].textY, words[i].text, false, words[i].style,
                      BidiUtils::BidiBaseDir::AUTO, words[i].tracking);
  }
  return saved;
}

bool PageTextPicker::drawCursorDelta(GfxRenderer& renderer) {
  if (words.empty() || !snapshotValid) return false;
  renderer.writeFramebufferRegion(snapshotBox.x, snapshotBox.y, snapshotBox.w, snapshotBox.h, snapshot.get());
  prewarmCursorGlyphs(renderer);
  return drawCursor(renderer);
}

void PageTextPicker::drawHints(GfxRenderer& renderer, const MappedInputManager& mappedInput,
                               const StrId wordConfirm) const {
  // Drawn last on every repaint path so the bar stays the top layer even when
  // the cursor box reaches under a hint's screen area.
  if (words.empty()) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  if (currentStage == Stage::Line) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_PICK_LINE), tr(STR_PREV_LINE), tr(STR_NEXT_LINE));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), I18N.get(wordConfirm), tr(STR_PREV_WORD), tr(STR_NEXT_WORD));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
