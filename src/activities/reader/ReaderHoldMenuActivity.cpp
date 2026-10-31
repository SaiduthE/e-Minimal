#include "ReaderHoldMenuActivity.h"

#include <HalDisplay.h>
#include <I18n.h>

void ReaderHoldMenuActivity::onEnter() {
  Activity::onEnter();

  const char* options[] = {tr(STR_HIGHLIGHT), tr(STR_DICTIONARY), tr(STR_BOOKMARK_OPTION)};
  holdPopup.show(tr(STR_HOLD_SELECT_ACTION), options, 3, 0, [this](int row) {
    ActivityResult res;
    res.data = MenuResult{row};
    setResult(std::move(res));
    finish();
  });

  requestUpdate(true);
}

void ReaderHoldMenuActivity::loop() {
  if (holdPopup.handleInput(mappedInput, [this] {
        selectionMoved = true;
        requestUpdate();
      })) return;

  // Dismissed without a pick (Back or a tap outside): cancel.
  ActivityResult res;
  res.isCancelled = true;
  setResult(std::move(res));
  finish();
}

// The first render and our own selection moves draw over the framebuffer, so
// the reader page stays visible behind the popup. Any other render (an overlay
// closing, the power menu) finds an unrelated framebuffer and clears first.
void ReaderHoldMenuActivity::render(RenderLock&& lock) {
  if (!holdPopup.isActive()) return;  // dismissed: the reader repaints

  const bool overDrawn = keepBackdrop && (firstRender || selectionMoved);
  firstRender = false;
  selectionMoved = false;
  if (!overDrawn) renderer.clearScreen();
  holdPopup.processRender(renderer, mappedInput);  // draws and refreshes
}
