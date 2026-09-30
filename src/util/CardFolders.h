#pragma once

// The SD card's standard folders, created at boot when missing, so a fresh card
// is ready without making any by hand: /Books and /Manga for reading, /sleep
// for sleep-screen pictures, /carousel (or the folder chosen in the web
// interface) for the Dashboard's Image widget, /fonts, /dictionaries and
// /screenshots. Existing folders and files are left alone.
namespace card_folders {

void ensure();

}  // namespace card_folders
