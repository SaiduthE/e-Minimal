#pragma once

#include <BoardConfig.h>
#include <GfxRenderer.h>

#include "CrossPointSettings.h"
#include "activities/reader/ReaderUtils.h"

// The orientation Home, menus and settings draw in. On e-Minimal the one
// orientation setting (Global Settings, Display Orientation) turns the whole
// device, reader included; elsewhere the UI stays portrait and only the reader
// follows the setting.
namespace ui_orientation {

inline bool followsSetting() { return BoardConfig::isEMinimal78(); }

inline void apply(GfxRenderer& renderer) {
  if (followsSetting()) {
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  } else {
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  }
}

}  // namespace ui_orientation
