#include "PowerProbe.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <HalPowerManager.h>
#include <Logging.h>
#include <Wire.h>
#include <esp_timer.h>

#include <atomic>
#include <climits>

#include "activities/RenderLock.h"
#include "util/TaskWatchdog.h"

namespace power_probe {
namespace {
// INA260 registers are big-endian. Current is positive into Vin+, i.e. the load's
// draw; the bus voltage is read at Vin+ (its VB jumper), which is SYS.
constexpr uint8_t INA260_ADDR = 0x40;
constexpr uint8_t INA260_REG_CONFIG = 0x00;
constexpr uint8_t INA260_REG_CURRENT = 0x01;
constexpr uint8_t INA260_REG_BUS = 0x02;
constexpr uint8_t INA260_REG_MFG_ID = 0xFE;
constexpr uint8_t INA260_REG_DIE_ID = 0xFF;
constexpr uint16_t INA260_MFG_ID = 0x5449;  // "TI"
constexpr uint16_t INA260_DIE_ID = 0x227;   // DIE_ID[15:4]; the INA226 shares the maker ID
// Shunt and bus continuous, 1.1 ms conversions, averaged x4: a fresh reading every
// ~8.8 ms, so 10 ms samples cover the whole window.
constexpr uint16_t INA260_CONFIG_WINDOW = 0x6327;
constexpr int32_t INA260_LSB_UA = 1250;
constexpr int32_t INA260_LSB_UV = 1250;
// For a light sleep the ESP cannot sample, so the INA260 averages by itself: 256 x
// (8.244 + 8.244) ms, a reading every ~4.2 s. A sleep of two windows or more ends
// with one wholly inside it, which is what the registers hold on the wake.
constexpr uint16_t INA260_CONFIG_SLEEP = 0x6BFF;
constexpr uint32_t SLEEP_AVERAGE_MS = 4221;
constexpr uint32_t MIN_SLEEP_S = 10;

constexpr uint8_t MAX17048_REG_CRATE = 0x16;  // signed, 0.208 %/h per LSB

constexpr uint32_t SAMPLE_PERIOD_MS = 10;
constexpr uint32_t MAX_WINDOW_S = 600;
constexpr uint32_t TASK_STACK_BYTES = 4096;  // printf with floats in the summary

std::atomic<bool> windowActive{false};
// The last light-sleep result: the sleep drops the USB console, so the line printed
// on the wake is usually lost and CMD:POWER LAST prints it again.
char lastSleepLine[160] = "";

TwoWire& bus() {
#if SOC_I2C_NUM > 1
  if (BoardConfig::ACTIVE.batteryGauge.i2cBus == 1) return Wire1;
#endif
  return Wire;
}

// Main-loop only. BatteryMonitor may have started the bus already; begin() then
// returns true without touching it.
bool ensureBus() {
  static bool begun = false;
  const auto& g = BoardConfig::ACTIVE.batteryGauge;
  if (g.i2cSda < 0 || g.i2cScl < 0) return false;
  if (!begun) begun = bus().begin(g.i2cSda, g.i2cScl, g.i2cHz);
  return begun;
}

bool read16(uint8_t addr, uint8_t reg, uint16_t& out) {
  TwoWire& w = bus();
  w.beginTransmission(addr);
  w.write(reg);
  if (w.endTransmission(false) != 0) return false;
  if (w.requestFrom(addr, static_cast<uint8_t>(2)) < 2) return false;
  const uint8_t hi = w.read();
  const uint8_t lo = w.read();
  out = static_cast<uint16_t>((hi << 8) | lo);
  return true;
}

bool write16(uint8_t addr, uint8_t reg, uint16_t value) {
  TwoWire& w = bus();
  w.beginTransmission(addr);
  w.write(reg);
  w.write(static_cast<uint8_t>(value >> 8));
  w.write(static_cast<uint8_t>(value & 0xFF));
  return w.endTransmission() == 0;
}

void windowTask(void* arg) {
  const int64_t windowUs = static_cast<int64_t>(reinterpret_cast<uintptr_t>(arg)) * 1000000;
  int64_t chargeUaUs = 0;
  int64_t energyUwUs = 0;
  int64_t busUvUs = 0;
  int64_t coveredUs = 0;  // time covered by good samples
  int32_t minUa = INT32_MAX;
  int32_t maxUa = INT32_MIN;
  uint32_t samples = 0;
  uint32_t errors = 0;

  const int64_t startUs = esp_timer_get_time();
  int64_t prevUs = startUs;
  TickType_t wake = xTaskGetTickCount();
  while (prevUs - startUs < windowUs) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    const int64_t nowUs = esp_timer_get_time();
    const int64_t dtUs = nowUs - prevUs;
    prevUs = nowUs;
    uint16_t rawI = 0;
    uint16_t rawV = 0;
    if (!read16(INA260_ADDR, INA260_REG_CURRENT, rawI) || !read16(INA260_ADDR, INA260_REG_BUS, rawV)) {
      ++errors;
      continue;
    }
    // Each reading stands for the interval since the previous one.
    const int32_t ua = static_cast<int16_t>(rawI) * INA260_LSB_UA;
    const int32_t uv = static_cast<int32_t>(rawV) * INA260_LSB_UV;
    chargeUaUs += static_cast<int64_t>(ua) * dtUs;
    energyUwUs += static_cast<int64_t>(ua) * uv / 1000000 * dtUs;
    busUvUs += static_cast<int64_t>(uv) * dtUs;
    coveredUs += dtUs;
    if (ua < minUa) minUa = ua;
    if (ua > maxUa) maxUa = ua;
    ++samples;
  }

  const double elapsedS = static_cast<double>(prevUs - startUs) / 1e6;
  if (coveredUs > 0) {
    const double avgMa = static_cast<double>(chargeUaUs) / coveredUs / 1000.0;
    const double avgMw = static_cast<double>(energyUwUs) / coveredUs / 1000.0;
    const double avgMv = static_cast<double>(busUvUs) / coveredUs / 1000.0;
    // Over the whole window at the sampled average, so a failed read is not zero draw.
    const double mwh = avgMw * elapsedS / 3600.0;
    logSerial.printf(
        "POWER %.1f s, %u samples, %u errors: avg %.1f mA (min %.1f, max %.1f), SYS %.0f mV, avg %.0f mW, %.3f mWh\n",
        elapsedS, (unsigned)samples, (unsigned)errors, avgMa, minUa / 1000.0, maxUa / 1000.0, avgMv, avgMw, mwh);
  } else {
    logSerial.printf("POWER %.1f s: no good samples, %u errors\n", elapsedS, (unsigned)errors);
  }
  windowActive = false;
  vTaskDelete(nullptr);
}

// The Meter answers and is an INA260, set to `config`; prints why not otherwise.
bool prepareMeter(uint16_t config) {
  if (windowActive) {
    logSerial.printf("POWER busy: a window is still running\n");
    return false;
  }
  uint16_t mfg = 0;
  uint16_t die = 0;
  if (!ensureBus() || !read16(INA260_ADDR, INA260_REG_MFG_ID, mfg) || !read16(INA260_ADDR, INA260_REG_DIE_ID, die)) {
    logSerial.printf("POWER no Meter at 0x%02X\n", INA260_ADDR);
    return false;
  }
  if (mfg != INA260_MFG_ID || (die >> 4) != INA260_DIE_ID) {
    logSerial.printf("POWER 0x%02X is not an INA260 (maker 0x%04X, die 0x%04X)\n", INA260_ADDR, mfg, die);
    return false;
  }
  if (!write16(INA260_ADDR, INA260_REG_CONFIG, config)) {
    logSerial.printf("POWER Meter config write failed\n");
    return false;
  }
  return true;
}
}  // namespace

bool windowRunning() { return windowActive; }

bool startWindow(uint32_t seconds) {
  if (seconds == 0 || seconds > MAX_WINDOW_S) {
    logSerial.printf("POWER window is 1..%u s\n", (unsigned)MAX_WINDOW_S);
    return false;
  }
  if (!prepareMeter(INA260_CONFIG_WINDOW)) return false;
  windowActive = true;
  // A transient task, freed when the window closes; core 0 keeps it off the UI loop's core.
  if (xTaskCreatePinnedToCore(windowTask, "PowerProbe", TASK_STACK_BYTES,
                              reinterpret_cast<void*>(static_cast<uintptr_t>(seconds)), 2, nullptr, 0) != pdPASS) {
    windowActive = false;
    LOG_ERR("PWR", "PowerProbe task create failed");
    logSerial.printf("POWER could not start the sampler\n");
    return false;
  }
  logSerial.printf("POWER sampling %u s\n", (unsigned)seconds);
  return true;
}

bool measureLightSleep(uint32_t seconds) {
  if (seconds < MIN_SLEEP_S || seconds > MAX_WINDOW_S) {
    logSerial.printf("POWER SLEEP is %u..%u s\n", (unsigned)MIN_SLEEP_S, (unsigned)MAX_WINDOW_S);
    return false;
  }
  if (!prepareMeter(INA260_CONFIG_SLEEP)) return false;
  logSerial.printf("POWER light sleep %u s, a key ends it early\n", (unsigned)seconds);

  HalPowerManager::LightSleepWake wake;
  const unsigned long startMs = millis();
  {
    RenderLock lock;  // a repaint is never frozen halfway
    resetTaskWatchdogIfSubscribed();
    wake = powerManager.lightSleep(seconds * 1000);
    resetTaskWatchdogIfSubscribed();
  }
  const unsigned long sleptMs = millis() - startMs;
  uint16_t rawI = 0;
  uint16_t rawV = 0;
  const bool read = read16(INA260_ADDR, INA260_REG_CURRENT, rawI) && read16(INA260_ADDR, INA260_REG_BUS, rawV);
  write16(INA260_ADDR, INA260_REG_CONFIG, INA260_CONFIG_WINDOW);

  // A sleep can drop the USB console; give the host a moment to take it back.
  for (int i = 0; i < 30 && !logSerial; i++) delay(100);
  if (wake == HalPowerManager::LightSleepWake::NotSupported) {
    logSerial.printf("POWER light sleep not supported on this board\n");
    return false;
  }
  if (!read) {
    logSerial.printf("POWER Meter read failed after the sleep\n");
    return false;
  }
  if (sleptMs < 2 * SLEEP_AVERAGE_MS) {
    logSerial.printf("POWER light sleep ended after %lu ms, too short for a clean average\n", sleptMs);
    return false;
  }
  const float ma = static_cast<int16_t>(rawI) * (INA260_LSB_UA / 1000.0f);
  const float mv = rawV * (INA260_LSB_UV / 1000.0f);
  snprintf(lastSleepLine, sizeof(lastSleepLine),
           "POWER light sleep %.1f s (%s): %.1f mA, SYS %.0f mV, %.0f mW (INA260 %.1f s average)", sleptMs / 1000.0f,
           wake == HalPowerManager::LightSleepWake::Timer ? "timer" : "key", ma, mv, ma * mv / 1000.0f,
           SLEEP_AVERAGE_MS / 1000.0f);
  logSerial.printf("%s\n", lastSleepLine);
  return true;
}

void printLastSleep() {
  logSerial.printf("%s\n", lastSleepLine[0] ? lastSleepLine : "POWER no light sleep measured yet");
}

bool readGaugeRate(float& percentPerHour) {
  const auto& g = BoardConfig::ACTIVE.batteryGauge;
  if (g.gaugeAddr == 0 || g.gaugeType != BoardConfig::GaugeType::Max17048) return false;
  uint16_t raw = 0;
  if (!ensureBus() || !read16(g.gaugeAddr, MAX17048_REG_CRATE, raw)) return false;
  percentPerHour = static_cast<int16_t>(raw) * 0.208f;
  return true;
}

}  // namespace power_probe
