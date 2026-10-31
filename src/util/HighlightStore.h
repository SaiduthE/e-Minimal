#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

class GfxRenderer;
class Page;

// Per-book text highlights, kept sorted by (spine, start) so a page query is a
// contiguous range. Ranges use the visible-codepoint offsets of bookmarks
// (TextBlock::wordVisibleOffset): [start, end) within one spine item.
//
// File: /.crosspoint/highlights/<book key>.hl, keyed like the bookmark file
// (BookmarkUtil::getBookmarkPath with a .hl extension). Layout, little endian:
//   'H' 'L' version(u8 = 1) count(u16), then count x {spine u16, start u32, end u32, snippet[64]}.
// Writes go to a .tmp file that is renamed over the old one.
class HighlightStore {
 public:
  static constexpr size_t MAX_HIGHLIGHTS = 256;
  static constexpr size_t SNIPPET_BYTES = 64;  // includes the NUL

  struct Highlight {
    uint16_t spine;
    uint32_t start;
    uint32_t end;
    char snippet[SNIPPET_BYTES];
  };

  // File holding a book's highlights (path arithmetic only, no file IO).
  static std::string pathForBook(const std::string& bookPath);
  // Carries a renamed/moved book's highlights to its new key (no-op without a file).
  // Returns false when the target exists or the rename failed.
  static bool moveForBook(const std::string& oldBook, const std::string& newBook);

  // Reads the book's highlights. A missing file yields an empty list; a
  // leftover .tmp from an interrupted save is recovered first.
  void load(const std::string& bookPath);

  // false when the store is full or the file could not be written (the list is unchanged).
  bool add(const Highlight& highlight);
  bool remove(size_t index);

  size_t size() const { return count; }
  bool empty() const { return count == 0; }
  const Highlight& at(size_t index) const { return items[index]; }

  // Index of the first highlight of `spine` that overlaps [start, end); -1 when none.
  int findOverlap(uint16_t spine, uint32_t start, uint32_t end) const;

  // Contiguous run of one spine's highlights, sorted by start.
  const Highlight* spineRange(uint16_t spine, size_t& count) const;

  // Appends `word` (after a separating space when `dst` is not empty) to the
  // NUL-terminated snippet in `dst` (SNIPPET_BYTES), never splitting a UTF-8 sequence.
  static void appendSnippetWord(char* dst, const char* word);

  // True when any word of `page` intersects one of the n ranges.
  static bool pageHasHighlight(const Page& page, const Highlight* ranges, size_t n);
  // Paints the light-grey band behind every highlighted word of `page`, one
  // band per run of adjacent highlighted words in a line (gaps included).
  // Call before the page text, after the font prewarm scan.
  static void paintPage(GfxRenderer& renderer, const Page& page, int fontId, int marginLeft, int marginTop,
                        const Highlight* ranges, size_t n);
  // One band box. 16-level (gray4) target: a true grey nibble; plain B/W
  // render: a dither; grayscale plane passes: nothing.
  static void paintBand(GfxRenderer& renderer, int x, int y, int width, int height);

 private:
  // Reads `path` into items/count. false when the file exists but was not read
  // (bad header, buffer allocation failed): its records are unknown.
  bool readFile();
  // Re-reads after a failed load; false while the file still can't be read, so
  // add()/remove() refuse instead of overwriting records they never loaded.
  bool ensureLoaded();
  bool save() const;
  std::string path;
  bool loadFailed = false;
  // MAX_HIGHLIGHTS slots (~19.5 KB), allocated only when the book has
  // highlights or on the first add, so books without any cost no heap.
  std::unique_ptr<Highlight[]> items;
  size_t count = 0;
};
