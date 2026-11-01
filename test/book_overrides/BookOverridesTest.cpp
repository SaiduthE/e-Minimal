#include <gtest/gtest.h>

#include "BookOverrides.h"

using bookmeta::NO_SERIES_INDEX;
using bookmeta::parseSeriesIndex;

TEST(ParseSeriesIndex, WholeAndFractionalNumbers) {
  EXPECT_EQ(parseSeriesIndex("2"), 200);
  EXPECT_EQ(parseSeriesIndex("2.5"), 250);
  EXPECT_EQ(parseSeriesIndex("2,5"), 250);
  EXPECT_EQ(parseSeriesIndex("0"), 0);
  EXPECT_EQ(parseSeriesIndex("10.25"), 1025);
  EXPECT_EQ(parseSeriesIndex(" 3 "), 300);
}

TEST(ParseSeriesIndex, ExtraPrecisionIsDropped) { EXPECT_EQ(parseSeriesIndex("1.239"), 123); }

TEST(ParseSeriesIndex, InvalidInputYieldsNoIndex) {
  EXPECT_EQ(parseSeriesIndex(nullptr), NO_SERIES_INDEX);
  EXPECT_EQ(parseSeriesIndex(""), NO_SERIES_INDEX);
  EXPECT_EQ(parseSeriesIndex("abc"), NO_SERIES_INDEX);
  EXPECT_EQ(parseSeriesIndex("-1"), NO_SERIES_INDEX);
  EXPECT_EQ(parseSeriesIndex("2."), NO_SERIES_INDEX);
  EXPECT_EQ(parseSeriesIndex("2x"), NO_SERIES_INDEX);
  EXPECT_EQ(parseSeriesIndex("99999"), NO_SERIES_INDEX);
}
