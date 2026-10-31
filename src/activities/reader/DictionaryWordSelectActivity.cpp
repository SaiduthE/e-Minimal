#include "DictionaryWordSelectActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <variant>

#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "components/UITheme.h"
#include "util/FiveButtonInput.h"

namespace {

constexpr unsigned long POPUP_DURATION_MS = 1500;
constexpr unsigned long WORD_REPEAT_START_MS = 500;
constexpr unsigned long WORD_REPEAT_INTERVAL_MS = 500;

void indexBuildYield(void*) { vTaskDelay(1); }

}  // namespace

void DictionaryWordSelectActivity::onEnter() {
  Activity::onEnter();
  fontId = SETTINGS.getReaderFontId();
  // Short tokens ("a", "of") are only skipped on the tap picker, where every
  // extra stop costs a button press.
  picker.build(renderer, *page, fontId, marginLeft, marginTop, five_button::active());
  picker.allocSnapshot(renderer);
  if (!five_button::active() && picker.wordCount() > 0) {
    // Start on the middle row's word nearest mid-screen instead of top-left:
    // any word on the page is then at most half a page of moves away.
    picker.selectWord(picker.closestInRow(picker.rowCount() / 2, renderer.getScreenWidth() / 2));
  }
  requestUpdate();
}

void DictionaryWordSelectActivity::onExit() {
  picker.freeSnapshot();
  Activity::onExit();
}

void DictionaryWordSelectActivity::moveVertical(const int direction) {
  const PageTextPicker::Word& current = picker.word(picker.selectedWord());
  const int best = picker.closestInRow(static_cast<int>(current.row) + direction, current.x + current.width / 2);
  if (best >= 0 && best != picker.selectedWord()) {
    picker.selectWord(best);
    cursorMoved = true;
    requestUpdate();
  }
}

void DictionaryWordSelectActivity::performLookup() {
  popup = Popup::Busy;
  if (!dictOpenAttempted) {
    dictOpenAttempted = true;
    dictOpenOk = dict.open(SETTINGS.dictionaryName);
    // needsIndex() opens and validates the .qidx sidecar, so ask it once per
    // open rather than once per word: the answer only changes when we build
    // the sidecar ourselves, which is handled below.
    dictNeedsIndex = dictOpenOk && dict.needsIndex();
  }
  popupMsg = dictNeedsIndex ? StrId::STR_DICT_INDEXING : StrId::STR_DICT_LOOKING_UP;
  requestUpdateAndWait();  // paint the page + busy popup before blocking on SD

  bool ok = dictOpenOk;
  Dictionary::IndexResult indexResult = Dictionary::IndexResult::Ok;
  if (ok && dictNeedsIndex) {
    ok = dict.buildIndex(&indexBuildYield, nullptr, &indexResult);
    dictNeedsIndex = !ok;  // a successful build leaves the sidecar fresh; a failed one retries
  }

  std::string definition;
  std::string headword;
  Dictionary::LookupResult result = Dictionary::LookupResult::NotFound;
  const bool found = ok && dict.lookup(picker.word(picker.selectedWord()).text, definition, headword, &result);

  if (found) {
    popup = Popup::None;
    picker.freeSnapshot();  // ~10 KB not held while the definition screen is up
    startActivityForResult(
        std::make_unique<DictionaryDefinitionActivity>(renderer, mappedInput, std::move(headword),
                                                       std::move(definition), dict.definitionsAreHtml()),
        [this](const ActivityResult& result) {
          // Confirm on the definition means "done": close this screen too.
          const auto* menu = std::get_if<MenuResult>(&result.data);
          if (menu && menu->action == DictionaryDefinitionActivity::RESULT_DONE) {
            finish();
            return;
          }
          picker.allocSnapshot(renderer);
          requestUpdate();
        });
    return;
  }
  // Name the failure: a genuine miss is "Not found"; a word that WAS found but
  // couldn't be read is a real error — and we distinguish decompression from a
  // low-memory allocation from a generic read error.
  if (!ok) {
    popup = Popup::Error;
    // An index build allocates a scan buffer, so it fails the same way lookups
    // do on a fragmented heap — name that rather than a generic error.
    switch (indexResult) {
      case Dictionary::IndexResult::LowMemory:
        popupMsg = StrId::STR_DICT_LOW_MEMORY;
        break;
      case Dictionary::IndexResult::ReadError:
        popupMsg = StrId::STR_DICT_READ_FAILED;
        break;
      case Dictionary::IndexResult::Ok:
      default:
        popupMsg = StrId::STR_DICT_ERROR;  // dict.open() failed, not the index
        break;
    }
  } else {
    switch (result) {
      case Dictionary::LookupResult::Decompress:
        popup = Popup::Error;
        popupMsg = StrId::STR_DICT_DECOMPRESS_ERROR;
        break;
      case Dictionary::LookupResult::LowMemory:
        popup = Popup::Error;
        popupMsg = StrId::STR_DICT_LOW_MEMORY;
        break;
      case Dictionary::LookupResult::ReadError:
        popup = Popup::Error;
        popupMsg = StrId::STR_DICT_READ_FAILED;
        break;
      case Dictionary::LookupResult::NotFound:
      default:
        popup = Popup::NotFound;
        popupMsg = StrId::STR_DICT_NOT_FOUND;
        break;
    }
  }
  popupTime = millis();
  requestUpdate();
}

void DictionaryWordSelectActivity::loop() {
  if (popup == Popup::NotFound || popup == Popup::Error) {
    if (millis() - popupTime >= POPUP_DURATION_MS) {
      popup = Popup::None;
      requestUpdate();
    }
    return;
  }

  if (picker.wordCount() == 0) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) finish();
    return;
  }

  // Five-button board: two-stage tap picker (line, then word).
  if (five_button::active()) {
    switch (picker.handleInput(mappedInput)) {
      case PageTextPicker::Event::Back:
        finish();
        break;
      case PageTextPicker::Event::Picked:
        performLookup();
        break;
      case PageTextPicker::Event::Moved:
        cursorMoved = true;
        requestUpdate();
        break;
      case PageTextPicker::Event::None:
        break;
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    performLookup();
    return;
  }

  // Touch: a touch-down moves the highlight to the touched word (differential
  // repaint), a tap on a word selects and looks it up in one go.
  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTouchDown(tx, ty)) {
    const int hit = picker.wordAt(tx, ty);
    if (hit >= 0 && hit != picker.selectedWord()) {
      picker.selectWord(hit);
      cursorMoved = true;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasScreenTapped(tx, ty)) {
    const int hit = picker.wordAt(tx, ty);
    if (hit >= 0) {
      picker.selectWord(hit);
      performLookup();
    }
    return;
  }

  const int selected = picker.selectedWord();
  const bool hasNextWord = selected + 1 < picker.wordCount();
  const unsigned long now = millis();
  const bool repeat =
      mappedInput.getHeldTime() >= WORD_REPEAT_START_MS && now - lastHorizontalMoveTime >= WORD_REPEAT_INTERVAL_MS;
  const bool moveLeft = mappedInput.wasPressed(MappedInputManager::Button::ScreenLeft) ||
                        (repeat && mappedInput.isPressed(MappedInputManager::Button::ScreenLeft));
  const bool moveRight = mappedInput.wasPressed(MappedInputManager::Button::ScreenRight) ||
                         (repeat && mappedInput.isPressed(MappedInputManager::Button::ScreenRight));
  if (moveLeft && selected > 0) {
    picker.selectWord(selected - 1);
    lastHorizontalMoveTime = now;
    cursorMoved = true;
    requestUpdate();
  } else if (moveRight && hasNextWord) {
    picker.selectWord(selected + 1);
    lastHorizontalMoveTime = now;
    cursorMoved = true;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenUp)) {
    moveVertical(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenDown)) {
    moveVertical(1);
  }
}

// Front-button bar. Boards with Left/Right keys keep the directional labels;
// the picker supplies the stage-specific hints on the five-button board.
void DictionaryWordSelectActivity::drawHints() const {
  if (five_button::active() || picker.wordCount() == 0) {
    picker.drawHints(renderer, mappedInput, StrId::STR_LOOKUP);
    return;
  }
  const auto labels = mappedInput.mapDirectionalLabels(tr(STR_BACK), tr(STR_LOOKUP), tr(STR_DIR_LEFT),
                                                       tr(STR_DIR_RIGHT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void DictionaryWordSelectActivity::render(RenderLock&&) {
  // Differential fast path: only the cursor moved and the framebuffer still
  // holds a clean page (no popup or sub-activity since the last full
  // repaint). Restore the pixels under the old cursor, draw the new one, and
  // push, skipping the two-pass page render entirely.
  // Only our own cursor moves qualify: a render requested by anyone else (an
  // overlay closing, the power menu) finds an unrelated framebuffer.
  const bool moved = cursorMoved;
  cursorMoved = false;
  if (moved && popup == Popup::None && picker.drawCursorDelta(renderer)) {
    drawHints();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }
  // No saved pixels (or the box did not fit): full repaint below.

  renderer.clearScreen();

  // Same prewarm-scan-then-render pass the reader uses, so SD-card fonts hit
  // the in-RAM glyph cache during the real draw.
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  page->render(renderer, fontId, marginLeft, marginTop);
  scope.endScanAndPrewarm();
  page->render(renderer, fontId, marginLeft, marginTop);

  picker.drawCursor(renderer);
  drawHints();

  if (popup != Popup::None) {
    // The popup overdraws the page, so the saved pixels no longer match the
    // framebuffer: force the next render onto the full-repaint path.
    picker.invalidateSnapshot();
    // drawPopup overlays the framebuffer and refreshes the display itself.
    // I18N.get directly: tr() only accepts literal key names.
    GUI.drawPopup(renderer, I18N.get(popupMsg));
    return;
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
