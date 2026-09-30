#pragma once

#include <string>
#include <vector>

// The image widget's data: the pictures (BMP, PNG, JPEG) of the folder set on
// the web UI's Dashboards page (default /carousel), in name order, the one showing
// (kept in state.json so it survives a reboot) and when the next one is due.
// The dashboard's tile and the full-screen view each own one.
class CarouselImages {
 public:
  // load() and step() take the render lock; unload() does not, since it runs
  // from onExit(), which ActivityManager already calls under it.
  void load();
  void unload();

  bool empty() const { return images.empty(); }
  const std::string& folder() const { return folderPath; }
  const std::string& currentName() const { return images[index]; }
  std::string currentPath() const;
  // millis() of the next change; 0 with fewer than two images.
  unsigned long nextChangeMs() const { return nextChange; }
  // Moves by delta images (wrapping) under the render lock, saves the
  // position and restarts the interval.
  void step(int delta);

 private:
  static constexpr size_t MAX_IMAGES = 500;

  std::string folderPath;
  std::vector<std::string> images;
  size_t index = 0;
  unsigned long intervalMs = 0;
  unsigned long nextChange = 0;
};
