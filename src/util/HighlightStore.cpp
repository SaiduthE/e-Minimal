#include "HighlightStore.h"

#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <climits>
#include <cstring>

#include "BookmarkUtil.h"

namespace {

constexpr char MAGIC_0 = 'H';
constexpr char MAGIC_1 = 'L';
constexpr uint8_t FORMAT_VERSION = 1;
constexpr size_t HEADER_BYTES = 5;
constexpr size_t RECORD_BYTES = 2 + 4 + 4 + HighlightStore::SNIPPET_BYTES;
// Gray4 nibble of the band (0x0 black .. 0xF white). 0xA is one of the four
// levels the fast (DU4) waveform can show, so it lands the same in both modes.
constexpr uint8_t BAND_GRAY4_LEVEL = 0xA;

bool lessByPosition(const HighlightStore::Highlight& a, const HighlightStore::Highlight& b) {
  return a.spine != b.spine ? a.spine < b.spine : a.start < b.start;
}

// True when [ws, we) intersects one of the n ranges (sorted by start). Words
// without source offsets (we == ws) never match.
bool wordHighlighted(const HighlightStore::Highlight* ranges, const size_t n, const uint32_t ws, const uint32_t we) {
  if (we <= ws) return false;
  for (size_t i = 0; i < n && ranges[i].start < we; i++) {
    if (ranges[i].end > ws) return true;
  }
  return false;
}

}  // namespace

std::string HighlightStore::pathForBook(const std::string& bookPath) {
  // Same key as the bookmark file, in the highlights directory.
  std::string key = BookmarkUtil::getBookmarkPath(bookPath);
  key.erase(0, key.find_last_of('/') + 1);
  const size_t dot = key.find_last_of('.');
  if (dot != std::string::npos) key.erase(dot);
  return "/.crosspoint/highlights/" + key + ".hl";
}

bool HighlightStore::moveForBook(const std::string& oldBook, const std::string& newBook) {
  const std::string oldPath = pathForBook(oldBook);
  const std::string newPath = pathForBook(newBook);
  if (oldPath == newPath || !Storage.exists(oldPath.c_str())) return true;
  if (Storage.exists(newPath.c_str()) || !Storage.rename(oldPath.c_str(), newPath.c_str())) {
    LOG_ERR("HLS", "Failed to move highlights %s -> %s", oldPath.c_str(), newPath.c_str());
    return false;
  }
  return true;
}

void HighlightStore::load(const std::string& bookPath) {
  path = pathForBook(bookPath);
  loadFailed = !readFile();
}

bool HighlightStore::ensureLoaded() {
  if (loadFailed) loadFailed = !readFile();
  return !loadFailed;
}

bool HighlightStore::readFile() {
  items.reset();
  count = 0;

  // save() removes the old file before renaming the new one in: a power cut in
  // between leaves only the complete .tmp.
  const std::string tmp = path + ".tmp";
  if (!Storage.exists(path.c_str()) && Storage.exists(tmp.c_str())) {
    if (!Storage.rename(tmp.c_str(), path.c_str())) LOG_ERR("HLS", "Could not recover %s", tmp.c_str());
  }

  HalFile file;
  if (!Storage.exists(path.c_str())) return true;
  if (!Storage.openFileForRead("HLS", path, file)) return false;

  uint8_t header[HEADER_BYTES];
  if (file.read(header, HEADER_BYTES) != static_cast<int>(HEADER_BYTES) || header[0] != MAGIC_0 ||
      header[1] != MAGIC_1 || header[2] != FORMAT_VERSION) {
    LOG_ERR("HLS", "Bad highlight file header: %s", path.c_str());
    return false;
  }
  const size_t stored = std::min<size_t>(header[3] | (header[4] << 8), MAX_HIGHLIGHTS);
  if (stored == 0) return true;
  items = makeUniqueNoThrow<Highlight[]>(MAX_HIGHLIGHTS);
  if (!items) {
    LOG_ERR("HLS", "Highlight buffer alloc failed (%u bytes)",
            static_cast<unsigned>(MAX_HIGHLIGHTS * sizeof(Highlight)));
    return false;
  }
  uint8_t record[RECORD_BYTES];
  for (size_t i = 0; i < stored; i++) {
    if (file.read(record, RECORD_BYTES) != static_cast<int>(RECORD_BYTES)) break;
    Highlight h;
    memcpy(&h.spine, record, 2);
    memcpy(&h.start, record + 2, 4);
    memcpy(&h.end, record + 6, 4);
    memcpy(h.snippet, record + 10, SNIPPET_BYTES);
    h.snippet[SNIPPET_BYTES - 1] = '\0';
    if (h.end > h.start) items[count++] = h;
  }
  std::sort(items.get(), items.get() + count, lessByPosition);
  LOG_DBG("HLS", "Loaded %u highlights", static_cast<unsigned>(count));
  return true;
}

bool HighlightStore::save() const {
  if (path.empty()) return false;
  Storage.mkdir("/.crosspoint/highlights");
  const std::string tmp = path + ".tmp";
  {
    HalFile file;
    if (!Storage.openFileForWrite("HLS", tmp, file)) return false;
    const uint8_t header[HEADER_BYTES] = {static_cast<uint8_t>(MAGIC_0), static_cast<uint8_t>(MAGIC_1), FORMAT_VERSION,
                                          static_cast<uint8_t>(count & 0xFF),
                                          static_cast<uint8_t>(count >> 8)};
    bool ok = file.write(header, HEADER_BYTES) == HEADER_BYTES;
    uint8_t record[RECORD_BYTES];
    for (size_t i = 0; ok && i < count; i++) {
      memcpy(record, &items[i].spine, 2);
      memcpy(record + 2, &items[i].start, 4);
      memcpy(record + 6, &items[i].end, 4);
      memcpy(record + 10, items[i].snippet, SNIPPET_BYTES);
      ok = file.write(record, RECORD_BYTES) == RECORD_BYTES;
    }
    if (!ok) {
      LOG_ERR("HLS", "Write failed: %s", tmp.c_str());
      file.close();
      Storage.remove(tmp.c_str());
      return false;
    }
    file.close();
  }
  if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
  if (!Storage.rename(tmp.c_str(), path.c_str())) {
    LOG_ERR("HLS", "Rename failed: %s", path.c_str());
    return false;
  }
  return true;
}

bool HighlightStore::add(const Highlight& highlight) {
  if (!ensureLoaded()) return false;
  if (count >= MAX_HIGHLIGHTS || highlight.end <= highlight.start) return false;
  if (!items) {
    items = makeUniqueNoThrow<Highlight[]>(MAX_HIGHLIGHTS);
    if (!items) {
      LOG_ERR("HLS", "Highlight buffer alloc failed (%u bytes)",
              static_cast<unsigned>(MAX_HIGHLIGHTS * sizeof(Highlight)));
      return false;
    }
  }
  Highlight* const begin = items.get();
  Highlight* const pos = std::upper_bound(begin, begin + count, highlight, lessByPosition);
  const size_t index = static_cast<size_t>(pos - begin);
  std::move_backward(pos, begin + count, begin + count + 1);
  begin[index] = highlight;
  count++;
  if (!save()) {
    std::move(begin + index + 1, begin + count, begin + index);
    count--;
    return false;
  }
  return true;
}

bool HighlightStore::remove(const size_t index) {
  if (!ensureLoaded() || index >= count) return false;
  Highlight* const begin = items.get();
  const Highlight removed = begin[index];
  std::move(begin + index + 1, begin + count, begin + index);
  count--;
  if (!save()) {
    std::move_backward(begin + index, begin + count, begin + count + 1);
    begin[index] = removed;
    count++;
    return false;
  }
  return true;
}

int HighlightStore::findOverlap(const uint16_t spine, const uint32_t start, const uint32_t end) const {
  for (size_t i = 0; i < count; i++) {
    if (items[i].spine == spine && items[i].start < end && items[i].end > start) return static_cast<int>(i);
  }
  return -1;
}

const HighlightStore::Highlight* HighlightStore::spineRange(const uint16_t spine, size_t& n) const {
  const Highlight* const begin = items.get();
  const Highlight* const end = begin + count;
  const Highlight* const first =
      std::lower_bound(begin, end, spine, [](const Highlight& h, const uint16_t s) { return h.spine < s; });
  const Highlight* const last =
      std::upper_bound(first, end, spine, [](const uint16_t s, const Highlight& h) { return s < h.spine; });
  n = static_cast<size_t>(last - first);
  return n == 0 ? nullptr : first;
}

void HighlightStore::appendSnippetWord(char* dst, const char* word) {
  size_t len = strlen(dst);
  if (len > 0 && len < SNIPPET_BYTES - 1) dst[len++] = ' ';
  const size_t wordLen = strlen(word);
  size_t take = std::min(wordLen, SNIPPET_BYTES - 1 - len);
  // Back off a partial UTF-8 sequence at the cut.
  if (take < wordLen) {
    while (take > 0 && (static_cast<uint8_t>(word[take]) & 0xC0) == 0x80) take--;
  }
  memcpy(dst + len, word, take);
  dst[len + take] = '\0';
}

bool HighlightStore::pageHasHighlight(const Page& page, const Highlight* ranges, const size_t n) {
  if (n == 0) return false;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* block = static_cast<const PageLine*>(element.get())->getBlock();
    if (!block || !block->valid()) continue;
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      if (wordHighlighted(ranges, n, block->wordVisibleOffset(i), block->wordVisibleEnd(i))) return true;
    }
  }
  return false;
}

void HighlightStore::paintBand(GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  if (renderer.getGray4Target()) {
    renderer.fillRectGray4(x, y, width, height, BAND_GRAY4_LEVEL);
  } else if (renderer.getRenderMode() == GfxRenderer::BW) {
    renderer.fillRectDither(x, y, width, height, LightGray);
  }
}

void HighlightStore::paintPage(GfxRenderer& renderer, const Page& page, const int fontId, const int marginLeft,
                               const int marginTop, const Highlight* ranges, const size_t n) {
  if (n == 0) return;
  const int lineHeight = renderer.getLineHeight(fontId);
  const int ascender = renderer.getFontAscenderSize(fontId);
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    const auto* block = line->getBlock();
    if (!block || !block->valid()) continue;
    const int y = line->yPos + marginTop + block->getRubyShift(ascender);
    const int8_t tracking = block->getBlockStyle().characterSpacing;
    const uint16_t words = block->wordCount();
    for (uint16_t i = 0; i < words;) {
      if (!wordHighlighted(ranges, n, block->wordVisibleOffset(i), block->wordVisibleEnd(i))) {
        i++;
        continue;
      }
      // One band over the run of adjacent highlighted words (min/max x also
      // covers bidi-reordered lines).
      int left = INT_MAX;
      int right = INT_MIN;
      for (; i < words && wordHighlighted(ranges, n, block->wordVisibleOffset(i), block->wordVisibleEnd(i)); i++) {
        const int x = line->xPos + block->wordXpos(i) + marginLeft;
        left = std::min(left, x);
        right = std::max(
            right, x + renderer.getTextAdvanceX(fontId, block->wordText(i), block->wordStyle(i), tracking));
      }
      paintBand(renderer, left, y, right - left, lineHeight);
    }
  }
}
