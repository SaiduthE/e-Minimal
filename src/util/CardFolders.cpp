#include "CardFolders.h"

#include <HalStorage.h>
#include <Logging.h>

#include "DashboardConfigStore.h"

namespace card_folders {
namespace {
constexpr const char* STANDARD[] = {"/Books", "/Manga", "/sleep", "/fonts", "/dictionaries", "/screenshots"};

void ensureFolder(const char* path) {
  if (Storage.exists(path)) return;
  if (Storage.mkdir(path)) {
    LOG_INF("CARD", "Created %s", path);
  } else {
    LOG_ERR("CARD", "Could not create %s", path);
  }
}
}  // namespace

void ensure() {
  for (const char* path : STANDARD) ensureFolder(path);
  // The Dashboard's picture folder: the default, or the one chosen in the web
  // interface (a missing settings file leaves the default).
  DASHBOARD_CONFIG.loadFromFile();
  ensureFolder(DASHBOARD_CONFIG.carouselFolder.c_str());
}

}  // namespace card_folders
