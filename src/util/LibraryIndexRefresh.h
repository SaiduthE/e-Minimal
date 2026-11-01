#pragma once

#include <LibraryIndexFile.h>

class GfxRenderer;

namespace library {

// Walk the card and reconcile the library index with the current "Use Book
// Metadata" setting. Logs the build stats; false when the build failed.
bool rebuildLibraryIndexFromSettings();

// Open `index`, rebuilding first (with the "rebuilding" popup) when the index is
// missing, marked dirty, or was built with the other metadata mode. Leaves `index`
// open on success; false when it could not be opened.
bool ensureLibraryIndexFresh(GfxRenderer& renderer, LibraryIndexFile& index);

}  // namespace library
