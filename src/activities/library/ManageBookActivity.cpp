#include "ManageBookActivity.h"

#include <BookOverrides.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "activities/network/CrossPointWebServerActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
std::string stemOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot > 0) name.resize(dot);
  return name;
}

// Percent-encodes everything outside the RFC 3986 unreserved set (so '/' becomes
// %2F), matching what the web page's URLSearchParams decodes.
std::string urlEncode(const std::string& in) {
  static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(in.size() * 3);
  for (const char ch : in) {
    const auto c = static_cast<unsigned char>(ch);
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out.push_back(ch);
    } else {
      out.push_back('%');
      out.push_back(HEX_DIGITS[c >> 4]);
      out.push_back(HEX_DIGITS[c & 0x0F]);
    }
  }
  return out;
}
}  // namespace

// Effective value per field: override if set, else the cached EPUB metadata
// (cache only, never a reparse), else the file name stem for the title.
void ManageBookActivity::loadDetails() {
  const size_t slash = bookPath.find_last_of('/');
  fileName = slash == std::string::npos ? bookPath : bookPath.substr(slash + 1);
  folder = (slash == std::string::npos || slash == 0) ? "/" : bookPath.substr(0, slash);

  std::string bookTitle;
  std::string bookAuthor;
  std::string bookSeries;
  std::string bookSeriesIndex;
  std::string bookGenre;
  if (FsHelpers::hasEpubExtension(bookPath)) {
    // Epub exceeds the stack budget; it lives only for this block.
    auto epub = makeUniqueNoThrow<Epub>(bookPath, "/.crosspoint");
    if (!epub) {
      LOG_ERR("MANAGE", "OOM: epub metadata");
    } else if (epub->load(false, true)) {
      bookTitle = epub->getTitle();
      bookAuthor = epub->getAuthor();
      bookSeries = epub->getSeries();
      bookSeriesIndex = epub->getSeriesIndex();
      bookGenre = epub->getGenre();
    }
  }

  // BookOverride is ~400 bytes: keep it off the task stack.
  auto ov = makeUniqueNoThrow<bookmeta::BookOverride>();
  if (!ov) {
    LOG_ERR("MANAGE", "OOM: override record");
    hasOverride = false;
  } else {
    hasOverride = bookmeta::load(bookPath.c_str(), *ov);
    if (!hasOverride) memset(ov.get(), 0, sizeof(*ov));
  }
  const auto pick = [&ov](const char* overrideValue, std::string& bookValue, Field& out) {
    out.edited = ov && overrideValue[0] != '\0';
    out.value = out.edited ? overrideValue : bookValue;
  };
  static constexpr char EMPTY[] = "";
  pick(ov ? ov->title : EMPTY, bookTitle, title);
  pick(ov ? ov->author : EMPTY, bookAuthor, author);
  pick(ov ? ov->series : EMPTY, bookSeries, series);
  pick(ov ? ov->genre : EMPTY, bookGenre, genre);
  if (title.value.empty()) title.value = stemOf(bookPath);

  Field index;
  pick(ov ? ov->seriesIndex : EMPTY, bookSeriesIndex, index);
  if (!series.value.empty() && !index.value.empty()) series.value += " #" + index.value;
  series.edited = series.edited || index.edited;

  if (selected >= rowCount()) selected = 0;
}

void ManageBookActivity::onEnter() {
  Activity::onEnter();
  loadDetails();
  requestUpdate();
}

void ManageBookActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const int count = rowCount();
  buttonNavigator.onNext([this, count] {
    selected = ButtonNavigator::nextIndex(selected, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    selected = ButtonNavigator::previousIndex(selected, count);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) activateRow(rowAt(selected));
}

void ManageBookActivity::activateRow(const Row row) {
  if (row == Row::EditOnPhone) {
    openEditOnPhone();
  } else {
    confirmClearOverrides();
  }
}

// The file-transfer flow raises the web server and shows its QR for
// http://<ip>/files?edit=<path>; the page opens that folder's Edit book dialog.
// Leaving it after the radio came on restarts the device (as File Transfer
// does everywhere), so this result only arrives when the user backed out first.
void ManageBookActivity::openEditOnPhone() {
  auto transfer = makeUniqueNoThrow<CrossPointWebServerActivity>(renderer, mappedInput,
                                                                 "files?edit=" + urlEncode(bookPath));
  if (!transfer) {
    LOG_ERR("MANAGE", "OOM: file transfer");
    return;
  }
  startActivityForResult(std::move(transfer), [this](const ActivityResult&) {
    loadDetails();
    requestUpdate();
  });
}

void ManageBookActivity::confirmClearOverrides() {
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_CLEAR_OVERRIDES_QUESTION), title.value);
  if (!confirmation) {
    LOG_ERR("MANAGE", "OOM: clear confirmation");
    return;
  }
  startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
    if (!result.isCancelled) {
      if (!bookmeta::remove(bookPath.c_str())) LOG_ERR("MANAGE", "cannot clear overrides for %s", bookPath.c_str());
      selected = 0;
    }
    loadDetails();
    requestUpdate();
  });
}

void ManageBookActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MANAGE_BOOK));

  const int x = metrics.contentSidePadding;
  const int textWidth = pageWidth - 2 * x;
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
  const int labelHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int valueHeight = renderer.getLineHeight(UI_10_FONT_ID);

  const auto drawField = [&](const char* label, const Field& field) {
    char labelBuf[64];
    if (field.edited) {
      snprintf(labelBuf, sizeof(labelBuf), "%s (%s)", label, tr(STR_MB_EDITED));
      label = labelBuf;
    }
    renderer.drawText(SMALL_FONT_ID, x, y, label, true);
    y += labelHeight;
    const std::string text = renderer.truncatedText(UI_10_FONT_ID, field.value.empty() ? "-" : field.value.c_str(),
                                                    textWidth);
    renderer.drawText(UI_10_FONT_ID, x, y, text.c_str(), true);
    y += valueHeight + metrics.verticalSpacing;
  };
  drawField(tr(STR_TITLE), title);
  drawField(tr(STR_MB_AUTHOR), author);
  drawField(tr(STR_MB_SERIES), series);
  drawField(tr(STR_MB_GENRE), genre);
  drawField(tr(STR_FILENAME), Field{fileName, false});
  drawField(tr(STR_MB_FOLDER), Field{folder, false});

  y += metrics.verticalSpacing;
  const int rowHeight = GUI.getMenuRowHeight(renderer);
  for (int i = 0; i < rowCount(); ++i) {
    const bool isSelected = i == selected;
    const char* label = rowAt(i) == Row::EditOnPhone ? tr(STR_EDIT_ON_PHONE) : tr(STR_CLEAR_OVERRIDES);
    if (isSelected) renderer.fillRect(x, y, textWidth, rowHeight, true);
    const int textY = y + (rowHeight - valueHeight) / 2;
    renderer.drawText(UI_10_FONT_ID, x + metrics.verticalSpacing, textY, label, !isSelected);
    y += rowHeight + metrics.menuSpacing;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
