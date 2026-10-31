#pragma once

#include "activities/Activity.h"
#include "components/OptionPopup.h"

// Five-button Hold Select popup: Highlight / Dictionary / Bookmark. Up/Down move
// (wrap), Confirm picks, Back cancels. A pick returns MenuResult{row index}; the
// reader maps the row to its action (EpubReaderActivity::openHoldMenu).
class ReaderHoldMenuActivity final : public Activity {
 public:
  // keepBackdrop: the framebuffer holds the reader page as a plain 1bpp frame,
  // so the popup may draw over it. False (page shown through the gray4 path)
  // clears the screen first.
  explicit ReaderHoldMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool keepBackdrop)
      : Activity("ReaderHoldMenu", renderer, mappedInput), keepBackdrop(keepBackdrop) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  OptionPopup holdPopup;
  const bool keepBackdrop;
  bool firstRender = true;
  bool selectionMoved = false;
};
