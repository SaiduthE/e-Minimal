#include "CarouselImages.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <ctime>
#include <string_view>

#include "CrossPointState.h"
#include "DashboardConfigStore.h"
#include "DashboardImage.h"
#include "activities/RenderLock.h"

namespace {
// When the next picture is due, in system time (which the RTC keeps through deep
// sleep); 0 = not scheduled since power-up. RTC memory, so a Dashboard deep
// sleep's timer wake finds the change it woke for.
RTC_DATA_ATTR time_t changeDueAt = 0;
}  // namespace

void CarouselImages::load() {
  DASHBOARD_CONFIG.loadFromFile();
  std::vector<std::string> found;
  const std::string folder = DASHBOARD_CONFIG.carouselFolder;

  auto dir = Storage.open(folder.c_str());
  if (dir && dir.isDirectory()) {
    // One pass to size the list, so the names land without regrowth.
    size_t count = 0;
    char name[256];
    for (auto file = dir.openNextFile(); file && count < MAX_IMAGES; file = dir.openNextFile()) {
      if (file.isDirectory()) continue;
      file.getName(name, sizeof(name));
      if (dashboard_image::isImage(name)) count++;
    }
    found.reserve(count);
    dir.rewindDirectory();
    for (auto file = dir.openNextFile(); file && found.size() < count; file = dir.openNextFile()) {
      if (file.isDirectory()) continue;
      file.getName(name, sizeof(name));
      if (dashboard_image::isImage(name)) found.emplace_back(name);
    }
    FsHelpers::sortFileList(found);
  } else {
    LOG_DBG("CAROUSEL", "No folder %s", folder.c_str());
  }

  {
    RenderLock lock;
    folderPath = folder;
    images = std::move(found);
    index = images.empty() ? 0 : APP_STATE.carouselIndex % images.size();
  }
  intervalMs = static_cast<unsigned long>(DASHBOARD_CONFIG.carouselIntervalMinutes) * 60UL * 1000UL;
  scheduleNext(false);
  LOG_DBG("CAROUSEL", "%u images in %s", static_cast<unsigned>(images.size()), folderPath.c_str());
}

void CarouselImages::unload() {
  images.clear();
  images.shrink_to_fit();
  index = 0;
  nextChange = 0;
}

std::string CarouselImages::currentPath() const {
  return folderPath == "/" ? "/" + images[index] : folderPath + "/" + images[index];
}

void CarouselImages::step(const int delta) {
  if (images.empty()) return;
  {
    RenderLock lock;
    const long size = static_cast<long>(images.size());
    index = static_cast<size_t>(((static_cast<long>(index) + delta) % size + size) % size);
  }
  APP_STATE.carouselIndex = static_cast<uint16_t>(index);
  APP_STATE.saveToFile();
  scheduleNext(true);
}

void CarouselImages::scheduleNext(const bool restart) {
  if (images.size() <= 1 || intervalMs == 0) {
    nextChange = 0;
    return;
  }
  const time_t now = time(nullptr);
  // Over a day late is not a missed change but a clock that was set since (the
  // forecast sets it): start the interval over.
  if (changeDueAt != 0 && now - changeDueAt > 24 * 3600) changeDueAt = 0;
  if (restart || changeDueAt == 0) changeDueAt = now + static_cast<time_t>(intervalMs / 1000);
  // A change that fell due while the device slept runs on the first pass.
  const time_t left = changeDueAt > now ? changeDueAt - now : 0;
  const unsigned long leftMs = static_cast<unsigned long>(left) * 1000UL;
  nextChange = millis() + (leftMs < intervalMs ? leftMs : intervalMs);
}
