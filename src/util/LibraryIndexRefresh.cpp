#include "LibraryIndexRefresh.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"

namespace library {

bool rebuildLibraryIndexFromSettings() {
  BuildStats stats;
  if (!buildLibraryIndex("/", stats, SETTINGS.libraryUseMetadata != 0)) {
    LOG_ERR("LIB", "index build failed");
    return false;
  }
  LOG_INF("LIB", "reconciled: %u unchanged, %u added, %u renamed, %u removed, %u enriched (%u dup, %u unreadable)",
          static_cast<unsigned>(stats.unchanged), static_cast<unsigned>(stats.added),
          static_cast<unsigned>(stats.renamed), static_cast<unsigned>(stats.removed),
          static_cast<unsigned>(stats.enriched), static_cast<unsigned>(stats.duplicatesDropped),
          static_cast<unsigned>(stats.unreadableSkipped));
  if (stats.dedupDegraded) LOG_ERR("LIB", "rebuild completed without duplicate detection");
  return true;
}

bool ensureLibraryIndexFresh(GfxRenderer& renderer, LibraryIndexFile& index) {
  const bool readMetadata = SETTINGS.libraryUseMetadata != 0;
  const bool rebuildNeeded =
      isLibraryIndexDirty() || !index.open(libraryIndexPath()) || index.header().metadataEnabled != readMetadata;
  if (!rebuildNeeded) return true;
  index.close();
  GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
  rebuildLibraryIndexFromSettings();
  if (!index.open(libraryIndexPath())) {
    LOG_ERR("LIB", "cannot open library index");
    return false;
  }
  return true;
}

}  // namespace library
