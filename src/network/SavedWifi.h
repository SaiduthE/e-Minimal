#pragma once

// Station-mode WiFi for background jobs that have no UI of their own (the
// Weather dashboard's daily fetch). Uses the networks saved from the WiFi
// screen: the last one that connected first, then any other saved network in
// range, strongest first. Blocking; the caller shows its own "updating" state.
namespace saved_wifi {

// True when at least one network is saved. Loads the credential store.
bool hasSavedNetworks();

// Connects to a saved network, waiting up to perNetworkTimeoutMs for each
// attempt. Returns true once connected.
bool connect(unsigned long perNetworkTimeoutMs = 10000);

// Radio off. Leaves the heap fragmented: a screen that goes on to the reader
// should silentRestart() on its way out, like the other WiFi activities.
void disconnect();

// True once connect() has run this boot, i.e. the heap wants that restart.
bool usedSinceBoot();

}  // namespace saved_wifi
