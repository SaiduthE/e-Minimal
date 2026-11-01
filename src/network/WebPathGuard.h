#pragma once

#include <strings.h>

#include <cstddef>
#include <cstring>

// Which SD paths the web interface, WebDAV and the upload WebSocket may touch. One rule for all of them: a path is
// protected when any segment (not just the last) is hidden, so /.crosspoint/wifi.json is refused as a whole.
namespace web_path {

inline bool isProtectedSegment(const char* seg, size_t len) {
  while (len > 0 && seg[0] == ' ') {  // FAT ignores leading spaces and trailing dots/spaces
    seg++;
    len--;
  }
  if (len > 0 && seg[0] == '.') return true;  // ".", "..", .crosspoint
  while (len > 0 && (seg[len - 1] == ' ' || seg[len - 1] == '.')) len--;
  if (len == 0) return false;
  // FAT also opens a long name through its 8.3 alias (CROSSP~1), which a name test cannot see.
  for (size_t i = 0; i + 1 < len; i++) {
    if (seg[i] == '~' && seg[i + 1] >= '0' && seg[i + 1] <= '9') return true;
  }
  static constexpr const char* HIDDEN[] = {"System Volume Information", "XTCache"};
  for (const char* item : HIDDEN) {
    if (strlen(item) == len && strncasecmp(seg, item, len) == 0) return true;
  }
  return false;
}

// True when any segment of `path` (separated by / or \) is protected. Dot-names and ".." are protected.
inline bool isProtected(const char* path) {
  if (!path) return true;
  const char* start = path;
  for (const char* p = path;; p++) {
    if (*p == '/' || *p == '\\' || *p == '\0') {
      if (p > start && isProtectedSegment(start, static_cast<size_t>(p - start))) return true;
      if (*p == '\0') break;
      start = p + 1;
    }
  }
  return false;
}

}  // namespace web_path
