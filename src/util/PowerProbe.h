#pragma once

#include <cstdint>

// e-Minimal bench power readout on the battery gauge's I2C bus: the INA260 Meter
// (0x40) in the load path between the charger and both boosts, and the MAX17048
// Gauge's charge rate. Console only (CMD:POWER, CMD:BATTERY); the product's
// battery reading goes through BatteryMonitor.
namespace power_probe {

// Samples the Meter every 10 ms for `seconds` (1..600) on a background task and
// prints one summary line when the window closes, so buttons keep working while
// it runs. Prints the reason and returns false when no Meter answers or a window
// is already running.
bool startWindow(uint32_t seconds);
// A window is sampling: a light sleep would freeze it and skew its average.
bool windowRunning();

// Light-sleeps for `seconds` (10..600, a key ends it early) with the panel as it
// is, then prints the Meter's own ~4.2 s average from the end of the sleep. The
// sampler cannot run while the ESP sleeps.
bool measureLightSleep(uint32_t seconds);

// Prints measureLightSleep()'s last result again: on this board a light sleep
// drops the USB console until the cable is replugged.
void printLastSleep();

// The Gauge's charge rate in %/h, positive while charging. false on I2C failure.
bool readGaugeRate(float& percentPerHour);

}  // namespace power_probe
