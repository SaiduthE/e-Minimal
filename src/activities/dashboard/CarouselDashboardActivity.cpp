#include "CarouselDashboardActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <cstdio>

#include "DashboardImage.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void CarouselDashboardActivity::onEnter() {
  TimedDashboardActivity::onEnter();
  images.load();
  requestUpdate();
}

void CarouselDashboardActivity::onExit() {
  Activity::onExit();
  images.unload();
}

void CarouselDashboardActivity::update() {
  images.step(1);
  requestUpdate();
}

bool CarouselDashboardActivity::handleKeys() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    images.step(1);
    requestUpdate();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    images.step(-1);
    requestUpdate();
    return true;
  }
  return false;
}

void CarouselDashboardActivity::drawMessage(const char* message) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_DASH_CAROUSEL));
  const int width = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const auto lines = renderer.wrappedText(UI_12_FONT_ID, message, width, 4);
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  int y = (renderer.getScreenHeight() - static_cast<int>(lines.size()) * lineHeight) / 2;
  for (const auto& line : lines) {
    renderer.drawCenteredText(UI_12_FONT_ID, y, line.c_str());
    y += lineHeight;
  }
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void CarouselDashboardActivity::draw(RenderLock&&) {
  char message[256];
  if (images.empty()) {
    snprintf(message, sizeof(message), tr(STR_DASH_CAROUSEL_EMPTY_FORMAT), images.folder().c_str());
    drawMessage(message);
    return;
  }
  // Grey where the panel can (one decode into the 16-level frame), else dithered.
  if (!dashboard_image::drawFullScreen(renderer, images.currentPath())) {
    snprintf(message, sizeof(message), tr(STR_DASH_CAROUSEL_BAD_FORMAT), images.currentName().c_str());
    drawMessage(message);
  }
}
