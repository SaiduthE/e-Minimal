#include <gtest/gtest.h>

#include <cstring>
#include <utility>
#include <vector>

#include "LibraryIndexFile.h"

namespace library {

std::string joinLibraryPath(const std::string_view folder, const std::string_view name) {
  return std::string(folder) + "/" + std::string(name);
}

}  // namespace library

namespace {

std::vector<uint8_t> makeBlob(const uint64_t pathHash, const std::initializer_list<uint8_t> fields) {
  std::vector<uint8_t> blob(sizeof(pathHash) + fields.size());
  std::memcpy(blob.data(), &pathHash, sizeof(pathHash));
  std::copy(fields.begin(), fields.end(), blob.begin() + sizeof(pathHash));
  return blob;
}

}  // namespace

TEST(LibraryIndexFile, MissingIndexDoesNotCloseAnUninitializedHandle) {
  Storage.clearFile();
  HalFile::resetInvalidCloseCount();

  {
    library::LibraryIndexFile index;
    EXPECT_FALSE(index.open("/missing.clx"));
  }

  EXPECT_EQ(HalFile::invalidCloseCount(), 0);
}

TEST(LibraryIndexFile, ReadsEveryStoredOrderInBothDirections) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 3;
  library::layoutSections(header, 0, 0);

  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  const uint16_t authorOrder[] = {2, 0, 1};
  const uint16_t arrivalOrder[] = {1, 2, 0};
  std::memcpy(bytes.data() + library::authorOrderOffset(header, 0), authorOrder, sizeof(authorOrder));
  std::memcpy(bytes.data() + library::arrivalOrderOffset(header, 0), arrivalOrder, sizeof(arrivalOrder));
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));

  const auto expectOrder = [&](const library::SortOrder order, const uint16_t a, const uint16_t b, const uint16_t c) {
    EXPECT_EQ(index.ordinalForRow(order, 0), a);
    EXPECT_EQ(index.ordinalForRow(order, 1), b);
    EXPECT_EQ(index.ordinalForRow(order, 2), c);
    EXPECT_EQ(index.ordinalForRow(order, 3), 0xFFFF);
  };
  expectOrder(library::SortOrder::RecentAsc, 1, 2, 0);
  expectOrder(library::SortOrder::RecentDesc, 0, 2, 1);
  expectOrder(library::SortOrder::TitleAsc, 0, 1, 2);
  expectOrder(library::SortOrder::TitleDesc, 2, 1, 0);
  expectOrder(library::SortOrder::AuthorAsc, 2, 0, 1);
  expectOrder(library::SortOrder::AuthorDesc, 1, 0, 2);
}

TEST(LibraryIndexFile, ResolvesRecentRowsByIdentity) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 3;
  // One 8-byte path hash blob per record.
  library::layoutSections(header, 0, 3 * sizeof(uint64_t));
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));

  // Ordinals 0 and 2 share a size, so only the hash can tell them apart.
  constexpr uint64_t HASHES[] = {11, 22, 33};
  constexpr uint32_t SIZES[] = {100, 200, 100};
  for (uint16_t ordinal = 0; ordinal < 3; ordinal++) {
    library::ClixRecord record{};
    record.fileSize = SIZES[ordinal];
    record.nameOff = ordinal * sizeof(uint64_t);
    std::memcpy(bytes.data() + library::recordOffset(header, ordinal), &record, sizeof(record));
    std::memcpy(bytes.data() + header.nameStart + record.nameOff, &HASHES[ordinal], sizeof(uint64_t));
  }
  const uint16_t arrivalOrder[] = {1, 2, 0};
  std::memcpy(bytes.data() + library::arrivalOrderOffset(header, 0), arrivalOrder, sizeof(arrivalOrder));
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  const library::BookIdentity books[] = {
      {HASHES[0], SIZES[0]},  // ordinal 0 -> ascending row 2
      {HASHES[2], SIZES[2]},  // same size as ordinal 0, hash picks ordinal 2 -> row 1
      {99, SIZES[0]},         // size matches, hash does not: absent
      {HASHES[1], 999},       // hash matches, size does not: absent
      {HASHES[1], 0},         // size unknown: the hash alone matches -> row 0
  };
  uint16_t rows[5] = {};
  ASSERT_TRUE(index.recentRowsFor(books, 5, rows));
  EXPECT_EQ(rows[0], 2);
  EXPECT_EQ(rows[1], 1);
  EXPECT_EQ(rows[2], 0xFFFF);
  EXPECT_EQ(rows[3], 0xFFFF);
  EXPECT_EQ(rows[4], 0);
}

TEST(LibraryIndexFile, RejectsInvalidPermutationOrdinal) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  library::layoutSections(header, 0, 0);
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  const uint16_t invalid = 1;
  std::memcpy(bytes.data() + library::authorOrderOffset(header, 0), &invalid, sizeof(invalid));
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0xFFFF);
}

TEST(LibraryIndexFile, ReadsPathHashAndEveryPublicBlobField) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  const uint8_t folder[] = {6, '/', 'b', 'o', 'o', 'k', 's'};
  constexpr uint64_t PATH_HASH = 0x0123456789ABCDEFULL;
  const auto blob = makeBlob(PATH_HASH, {'x', 1, 'a', 1, 't', 8, 'O', 'r', 'i', 'g', 'i', 'n', 'a', 'l'});
  header.folderCount = 1;
  library::layoutSections(header, sizeof(folder), blob.size());
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + header.folderStart, folder, sizeof(folder));
  std::memcpy(bytes.data() + header.nameStart, blob.data(), blob.size());
  library::ClixRecord record{};
  record.nameLen = 1;
  Storage.setFile("/library.clx", bytes);

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  uint64_t pathHash = 0;
  ASSERT_TRUE(index.readPathHash(record, pathHash));
  EXPECT_EQ(pathHash, PATH_HASH);
  std::string name;
  ASSERT_TRUE(index.readName(record, name));
  EXPECT_EQ(name, "x");
  std::string author;
  ASSERT_TRUE(index.readAuthor(record, author));
  EXPECT_EQ(author, "a");
  std::string title;
  ASSERT_TRUE(index.readTitle(record, title));
  EXPECT_EQ(title, "t");
  ASSERT_TRUE(index.readSourceAuthor(record, author));
  EXPECT_EQ(author, "Original");
  std::string path;
  ASSERT_TRUE(index.readPath(record, path));
  EXPECT_EQ(path, "/books/x");
  index.close();

  bytes[header.nameStart + sizeof(PATH_HASH) + 5] = 255;
  Storage.setFile("/library.clx", std::move(bytes));
  ASSERT_TRUE(index.open("/library.clx"));
  EXPECT_FALSE(index.readSourceAuthor(record, author));
}

TEST(LibraryIndexFile, RejectsTruncatedAndOverflowingPathHashes) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  library::layoutSections(header, 0, sizeof(uint64_t) - 1);
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  library::ClixRecord record{};
  uint64_t hash = 1;
  EXPECT_FALSE(index.readPathHash(record, hash));
  EXPECT_EQ(hash, 0u);
  EXPECT_TRUE(index.ioFailed());

  record.nameOff = UINT32_MAX;
  EXPECT_FALSE(index.readPathHash(record, hash));
}

TEST(LibraryIndexFile, RejectsFolderRecordBeyondFolderBlob) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  const uint8_t folder[] = {5, '/'};
  const auto blob = makeBlob(1, {'x', 0, 0, 0});
  library::layoutSections(header, sizeof(folder), blob.size());
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + header.folderStart, folder, sizeof(folder));
  std::memcpy(bytes.data() + header.nameStart, blob.data(), blob.size());
  library::ClixRecord record{};
  record.nameLen = 1;
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  std::string path;
  EXPECT_FALSE(index.readPath(record, path));
}

TEST(LibraryIndexFile, ReadsSeriesAndGenreViews) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 4;
  header.seriesBookCount = 3;
  header.seriesGroupCount = 2;
  header.genreBookCount = 1;
  header.genreGroupCount = 1;
  library::layoutSections(header, 0, 0);
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));

  const uint16_t numbers[] = {200, library::CLIX_NO_SERIES_NUMBER, 100, 100};
  std::memcpy(bytes.data() + library::seriesNumberOffset(header, 0), numbers, sizeof(numbers));
  // Series order: ordinals 2, 0 form "Alpha"; ordinal 3 forms "Beta".
  const uint16_t seriesOrder[] = {2, 0, 3};
  for (uint16_t k = 0; k < 3; k++) {
    std::memcpy(bytes.data() + library::seriesOrderOffset(header, k), &seriesOrder[k], sizeof(uint16_t));
  }
  const auto putGroup = [&](const uint32_t at, const uint16_t start, const uint16_t count, const char* name) {
    library::ClixGroup group{};
    group.start = start;
    group.count = count;
    group.nameLen = static_cast<uint8_t>(std::strlen(name));
    std::memcpy(group.name, name, group.nameLen);
    std::memcpy(bytes.data() + at, &group, sizeof(group));
  };
  putGroup(library::seriesGroupOffset(header, 0), 0, 2, "Alpha");
  putGroup(library::seriesGroupOffset(header, 1), 2, 1, "Beta");
  const uint16_t genreOrdinal = 1;
  std::memcpy(bytes.data() + library::genreOrderOffset(header, 0), &genreOrdinal, sizeof(genreOrdinal));
  putGroup(library::genreGroupOffset(header, 0), 0, 1, "Horror");
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  EXPECT_EQ(index.rowCount(library::SortOrder::SeriesAsc), 3);
  EXPECT_EQ(index.rowCount(library::SortOrder::GenreAsc), 1);
  EXPECT_EQ(index.rowCount(library::SortOrder::TitleAsc), 4);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::SeriesAsc, 0), 2);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::SeriesAsc, 2), 3);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::SeriesAsc, 3), 0xFFFF);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::GenreAsc, 0), 1);
  EXPECT_EQ(index.groupForRow(library::GroupKind::Series, 1), 0);
  EXPECT_EQ(index.groupForRow(library::GroupKind::Series, 2), 1);
  library::ClixGroup group{};
  ASSERT_TRUE(index.readGroup(library::GroupKind::Series, 1, group));
  EXPECT_EQ(library::LibraryIndexFile::groupName(group), "Beta");
  EXPECT_FALSE(index.readGroup(library::GroupKind::Series, 2, group));
  EXPECT_EQ(index.readSeriesNumber(0), 200);
  EXPECT_EQ(index.readSeriesNumber(1), library::CLIX_NO_SERIES_NUMBER);
}
