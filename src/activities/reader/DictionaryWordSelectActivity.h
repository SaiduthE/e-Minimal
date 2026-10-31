#pragma once

#include <Epub/Page.h>
#include <I18n.h>

#include <memory>

#include "PageTextPicker.h"
#include "activities/Activity.h"
#include "util/Dictionary.h"

// Word selection over the current reader page. Five-button board: a two-stage
// tap picker (PageTextPicker) - pick a line, then a word in it; Confirm on the
// word looks it up and opens DictionaryDefinitionActivity. Boards with
// Left/Right keys or touch step through words in reading order (Up/Down jump
// rows) and a tap on a word looks it up directly. Back leaves the picker (one
// stage up on the five-button board), then returns to the reader.
class DictionaryWordSelectActivity final : public Activity {
 public:
  explicit DictionaryWordSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        std::unique_ptr<Page> page, int marginLeft, int marginTop)
      : Activity("DictionaryWordSelect", renderer, mappedInput),
        page(std::move(page)),
        marginLeft(marginLeft),
        marginTop(marginTop) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Popup : uint8_t { None, Busy, NotFound, Error };

  void moveVertical(int direction);
  void performLookup();
  void drawHints() const;

  std::unique_ptr<Page> page;
  const int marginLeft;
  const int marginTop;
  int fontId = 0;

  PageTextPicker picker;
  bool cursorMoved = false;  // set by our own cursor moves; gates the differential repaint
  unsigned long lastHorizontalMoveTime = 0;

  Dictionary dict;
  bool dictOpenAttempted = false;
  bool dictOpenOk = false;
  bool dictNeedsIndex = false;

  Popup popup = Popup::None;
  StrId popupMsg = StrId::STR_DICT_NOT_FOUND;
  unsigned long popupTime = 0;
};
