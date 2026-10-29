#pragma once

#include <Xtc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <memory>
#include <string>

#include "ReaderActivity.h"

// A 2-bit page read from SD on core 0 while the panel send runs on the reader's
// task: the stream task fills the page buffer in chunks, in the order the
// driver asks for rows, and the row fill waits on rowsReady (renderPage()).
struct XtcPageStream {
  const Xtc* xtc = nullptr;
  uint8_t* buffer = nullptr;  // both planes, at the offsets loadPage() would use
  uint64_t dataOffset = 0;    // the bitmap's file offset
  size_t planeSize = 0;
  size_t rowBytes = 0;
  uint16_t rows = 0;
  uint16_t chunkRows = 0;
  bool flipped = false;  // panel row 0 is the end of each plane
  std::atomic<uint16_t> rowsReady{0};
  std::atomic<bool> failed{false};
  uint32_t waitUs = 0;  // time the row fill spent waiting on SD
  SemaphoreHandle_t start = nullptr;  // a page posted
  SemaphoreHandle_t chunk = nullptr;  // a chunk landed
  SemaphoreHandle_t done = nullptr;   // the page is read; the buffer is the caller's again
  std::atomic<bool> quit{false};
  std::atomic<bool> exited{false};
  TaskHandle_t task = nullptr;
};

class XtcReaderActivity final : public ReaderActivity {
  std::shared_ptr<Xtc> xtc;
  uint32_t currentPage = 0;
  // The page just shown was mostly ink: flash white before the next one.
  bool prevPageDark = false;
  static constexpr uint32_t DARK_PAGE_PERCENT = 45;
  XtcPageStream stream;

  void startStreamTask();
  void stopStreamTask();

  enum class StatusBarOverlayPosition { Bottom, Top };
  struct StatusBarInfo {
    int currentPage;
    int pageCount;
    std::string title;
  };

  void renderPage();
  void openMenu();
  void openChapterSelection();
  void renderStatusBarOverlay(GfxRenderer& renderer, StatusBarOverlayPosition position) const;
  StatusBarInfo getStatusBarInfo() const;
  void saveProgress() const;
  void loadProgress();

  bool loadBook() override;
  std::string getBookTitle() const override { return xtc ? xtc->getTitle() : ""; }
  std::string getBookAuthor() const override { return xtc ? xtc->getAuthor() : ""; }
  std::string getBookThumbBmpPath() const override { return xtc ? xtc->getThumbBmpPath() : ""; }
  bool handleFormatInput() override;
  void renderBook() override;
  void applyInitialOrientation() override;

 public:
  explicit XtcReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("XtcReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~XtcReaderActivity() override = default;

  void onEnter() override;
  void onExit() override;
  bool pageTurn(bool isForward) override;
  bool isRightToLeft() const override { return xtc && xtc->isRightToLeft(); }
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  ScreenshotInfo getScreenshotInfo() const override;
};
