#pragma once

#include <I18n.h>

// Translated day and month names for the dashboard (tm_wday / tm_mon indexes).
namespace date_names {

inline const char* weekday(const int wday) {
  static constexpr StrId NAMES[7] = {StrId::STR_SUNDAY,   StrId::STR_MONDAY, StrId::STR_TUESDAY, StrId::STR_WEDNESDAY,
                                     StrId::STR_THURSDAY, StrId::STR_FRIDAY, StrId::STR_SATURDAY};
  return I18N.get(NAMES[((wday % 7) + 7) % 7]);
}

inline const char* weekdayShort(const int wday) {
  static constexpr StrId NAMES[7] = {StrId::STR_SUN_SHORT, StrId::STR_MON_SHORT, StrId::STR_TUE_SHORT,
                                     StrId::STR_WED_SHORT, StrId::STR_THU_SHORT, StrId::STR_FRI_SHORT,
                                     StrId::STR_SAT_SHORT};
  return I18N.get(NAMES[((wday % 7) + 7) % 7]);
}

inline const char* month(const int mon) {
  static constexpr StrId NAMES[12] = {StrId::STR_JANUARY,   StrId::STR_FEBRUARY, StrId::STR_MARCH,
                                      StrId::STR_APRIL,     StrId::STR_MAY,      StrId::STR_JUNE,
                                      StrId::STR_JULY,      StrId::STR_AUGUST,   StrId::STR_SEPTEMBER,
                                      StrId::STR_OCTOBER,   StrId::STR_NOVEMBER, StrId::STR_DECEMBER};
  return I18N.get(NAMES[((mon % 12) + 12) % 12]);
}

}  // namespace date_names
