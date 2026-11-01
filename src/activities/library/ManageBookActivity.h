#pragma once

#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Per-book details screen: the effective title, author, series and genre (the
// user's override where set, else the book's own), the file name and folder.
// Rows: "Edit on phone" (file transfer with a QR that deep-links the web UI's
// Edit book dialog) and "Clear edited details" (only while an override exists).
// Back leaves. Overrides themselves live in bookmeta (lib/BookMeta).
class ManageBookActivity final : public Activity {
 public:
  explicit ManageBookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath)
      : Activity("ManageBook", renderer, mappedInput), bookPath(std::move(bookPath)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Row : uint8_t { EditOnPhone, ClearOverrides };

  struct Field {
    std::string value;
    bool edited = false;
  };

  const std::string bookPath;
  Field title;
  Field author;
  Field series;  // name, then " #N" appended when the book has an index
  Field genre;
  std::string fileName;
  std::string folder;
  bool hasOverride = false;
  int selected = 0;
  ButtonNavigator buttonNavigator;

  void loadDetails();
  int rowCount() const { return hasOverride ? 2 : 1; }
  Row rowAt(const int index) const { return index == 0 ? Row::EditOnPhone : Row::ClearOverrides; }
  void activateRow(Row row);
  void openEditOnPhone();
  void confirmClearOverrides();
};
