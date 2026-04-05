#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>

namespace {

constexpr uint8_t PIN_VEXT_ENABLE = 36;   // Active low: powers OLED and antenna boost.
constexpr uint8_t PIN_LED = 35;
constexpr uint8_t PIN_LORA_RESET = 12;
constexpr uint8_t PIN_LORA_DIO1 = 14;
constexpr uint8_t PIN_LORA_BUSY = 13;
constexpr uint8_t PIN_LORA_CS = 8;
constexpr uint8_t PIN_LORA_SCK = 9;
constexpr uint8_t PIN_LORA_MISO = 11;
constexpr uint8_t PIN_LORA_MOSI = 10;

constexpr uint8_t PIN_FEM_POWER = 7;      // VFEM_Ctrl.
constexpr uint8_t PIN_FEM_ENABLE = 2;     // CSD.
constexpr uint8_t PIN_FEM_RX_MODE = 5;    // KCT8103L CTX, low = RX path.
constexpr uint8_t PIN_FEM_TX_MODE = 46;   // GC1109 CPS, high = PA path.

constexpr float TCXO_VOLTAGE = 1.8f;
constexpr int8_t DEFAULT_TX_POWER_DBM = 20;
constexpr uint16_t PREAMBLE_LEN = 16;
constexpr uint8_t SYNC_WORD_PRIVATE = 0x12;
constexpr uint8_t SYNC_WORD_PUBLIC = 0x34;

struct ScanProfile {
  const char *name;
  float frequencyMHz;
  float bandwidthKHz;
  uint8_t spreadingFactor;
  uint8_t codingRate;
};

struct RadioOptions {
  uint8_t syncWord = SYNC_WORD_PRIVATE;
  bool crcEnabled = true;
  bool boostedGain = false;
  uint16_t preambleLen = PREAMBLE_LEN;
};

constexpr ScanProfile PROFILES[] = {
    {"RU MEDIUM_FAST slot0", 868.825f, 250.0f, 11, 5},
    {"RU MEDIUM_FAST slot1", 869.075f, 250.0f, 11, 5},
    {"EU_868 MEDIUM_FAST",   869.525f, 250.0f, 11, 5},
};

constexpr int8_t POWER_SWEEP_DBM[] = {2, 5, 8, 11, 14, 17, 20};

enum class FemMode : uint8_t {
  Rx,
  Tx,
};

SX1262 radio = new Module(PIN_LORA_CS, PIN_LORA_DIO1, PIN_LORA_RESET, PIN_LORA_BUSY);

volatile bool packetReceivedFlag = false;
uint32_t txCounter = 0;
size_t activeProfileIndex = 0;

void setLed(bool on) {
  digitalWrite(PIN_LED, on ? HIGH : LOW);
}

void applyFemMode(FemMode mode) {
  // Heltec V4 uses an external front-end module. We keep the FEM powered and
  // explicitly switch between RX and TX paths so diagnostics reflect the RF path.
  digitalWrite(PIN_FEM_POWER, HIGH);
  digitalWrite(PIN_FEM_ENABLE, HIGH);

  switch (mode) {
    case FemMode::Rx:
      digitalWrite(PIN_FEM_RX_MODE, LOW);
      digitalWrite(PIN_FEM_TX_MODE, LOW);
      break;
    case FemMode::Tx:
      digitalWrite(PIN_FEM_RX_MODE, HIGH);
      digitalWrite(PIN_FEM_TX_MODE, HIGH);
      break;
  }
}

void configurePins() {
  pinMode(PIN_VEXT_ENABLE, OUTPUT);
  digitalWrite(PIN_VEXT_ENABLE, LOW);

  pinMode(PIN_FEM_POWER, OUTPUT);
  pinMode(PIN_FEM_ENABLE, OUTPUT);
  pinMode(PIN_FEM_RX_MODE, OUTPUT);
  pinMode(PIN_FEM_TX_MODE, OUTPUT);

  pinMode(PIN_LED, OUTPUT);
  setLed(false);
  applyFemMode(FemMode::Rx);
}

void printRadioState(const char *label, int state) {
  Serial.printf("[%s] state=%d\r\n", label, state);
}

const ScanProfile &activeProfile() {
  return PROFILES[activeProfileIndex];
}

bool configureRadio(
    const ScanProfile &profile,
    int8_t txPowerDbm = DEFAULT_TX_POWER_DBM,
    const RadioOptions &options = {}) {
  Serial.printf(
      "\r\n[radio] profile=%s freq=%.3f MHz bw=%.1f kHz sf=%u cr=4/%u tx=%d dBm sync=0x%02X crc=%s gain=%s preamble=%u\r\n",
      profile.name,
      profile.frequencyMHz,
      profile.bandwidthKHz,
      profile.spreadingFactor,
      profile.codingRate,
      txPowerDbm,
      options.syncWord,
      options.crcEnabled ? "on" : "off",
      options.boostedGain ? "boosted" : "normal",
      options.preambleLen);

  applyFemMode(FemMode::Rx);

  int state = radio.begin(
      profile.frequencyMHz,
      profile.bandwidthKHz,
      profile.spreadingFactor,
      profile.codingRate,
      options.syncWord,
      txPowerDbm,
      options.preambleLen,
      TCXO_VOLTAGE,
      false);
  if (state != RADIOLIB_ERR_NONE) {
    printRadioState("begin", state);
    return false;
  }

  state = radio.setDio2AsRfSwitch(true);
  if (state != RADIOLIB_ERR_NONE) {
    printRadioState("setDio2AsRfSwitch", state);
  }

  state = radio.setCurrentLimit(140.0f);
  if (state != RADIOLIB_ERR_NONE) {
    printRadioState("setCurrentLimit", state);
  }

  state = radio.setCRC(options.crcEnabled);
  if (state != RADIOLIB_ERR_NONE) {
    printRadioState("setCRC", state);
  }

  state = radio.setRxBoostedGainMode(options.boostedGain);
  if (state != RADIOLIB_ERR_NONE) {
    printRadioState("setRxBoostedGainMode", state);
  }

  return true;
}

bool isPrintablePayload(const String &payload) {
  for (size_t i = 0; i < payload.length(); ++i) {
    const char ch = payload.charAt(i);
    if (ch == '\r' || ch == '\n' || ch == '\t') {
      continue;
    }
    if (ch < 32 || ch > 126) {
      return false;
    }
  }
  return true;
}

String toHexString(const String &payload) {
  String out;
  out.reserve(payload.length() * 3);
  for (size_t i = 0; i < payload.length(); ++i) {
    if (i > 0) {
      out += ' ';
    }
    char buf[4];
    snprintf(buf, sizeof(buf), "%02X", static_cast<uint8_t>(payload.charAt(i)));
    out += buf;
  }
  return out;
}

void printFemState() {
  Serial.printf(
      "[fem] power=%d enable=%d rx_pin=%d tx_pin=%d\r\n",
      digitalRead(PIN_FEM_POWER),
      digitalRead(PIN_FEM_ENABLE),
      digitalRead(PIN_FEM_RX_MODE),
      digitalRead(PIN_FEM_TX_MODE));
}

void printPins() {
  Serial.printf(
      "[pins] SPI sck=%u miso=%u mosi=%u cs=%u rst=%u dio1=%u busy=%u led=%u\r\n",
      PIN_LORA_SCK,
      PIN_LORA_MISO,
      PIN_LORA_MOSI,
      PIN_LORA_CS,
      PIN_LORA_RESET,
      PIN_LORA_DIO1,
      PIN_LORA_BUSY,
      PIN_LED);
  Serial.printf(
      "[pins] FEM vext=%u power=%u enable=%u rx=%u tx=%u\r\n",
      PIN_VEXT_ENABLE,
      PIN_FEM_POWER,
      PIN_FEM_ENABLE,
      PIN_FEM_RX_MODE,
      PIN_FEM_TX_MODE);
  printFemState();
}

void printActiveProfile() {
  const ScanProfile &profile = activeProfile();
  Serial.printf("[profile] active=%u '%s' freq=%.3f bw=%.1f sf=%u cr=4/%u\r\n",
                static_cast<unsigned>(activeProfileIndex + 1),
                profile.name,
                profile.frequencyMHz,
                profile.bandwidthKHz,
                profile.spreadingFactor,
                profile.codingRate);
}

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void setPacketReceivedFlag() {
  packetReceivedFlag = true;
}

bool startReceiveMode() {
  packetReceivedFlag = false;
  radio.setPacketReceivedAction(setPacketReceivedFlag);
  applyFemMode(FemMode::Rx);
  const int state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    printRadioState("startReceive", state);
    return false;
  }
  return true;
}

void runCadWindow(const ScanProfile &profile, uint32_t windowMs) {
  if (!configureRadio(profile)) {
    return;
  }

  applyFemMode(FemMode::Rx);

  uint32_t startedAt = millis();
  uint32_t cadHits = 0;
  uint32_t cadChecks = 0;

  while (millis() - startedAt < windowMs) {
    cadChecks++;
    const int state = radio.scanChannel();
    if (state == RADIOLIB_LORA_DETECTED) {
      cadHits++;
      setLed(true);
      Serial.printf(
          "[cad] hit=%lu/%lu profile=%s rssi=%.2f snr=%.2f\r\n",
          static_cast<unsigned long>(cadHits),
          static_cast<unsigned long>(cadChecks),
          profile.name,
          radio.getRSSI(),
          radio.getSNR());
      delay(40);
      setLed(false);
    } else if (state != RADIOLIB_CHANNEL_FREE) {
      Serial.printf("[cad] unexpected state=%d profile=%s\r\n", state, profile.name);
    }

    delay(25);
  }

  Serial.printf(
      "[cad] summary profile=%s checks=%lu hits=%lu\r\n",
      profile.name,
      static_cast<unsigned long>(cadChecks),
      static_cast<unsigned long>(cadHits));
}

void runReceiveWindow(
    const ScanProfile &profile,
    uint32_t windowMs,
    const RadioOptions &options = {},
    const char *label = "rx") {
  if (!configureRadio(profile, DEFAULT_TX_POWER_DBM, options)) {
    return;
  }

  Serial.printf("[%s] listening on %s for %lu ms\r\n",
                label,
                profile.name,
                static_cast<unsigned long>(windowMs));

  if (!startReceiveMode()) {
    return;
  }

  uint32_t startedAt = millis();
  uint32_t okCount = 0;
  uint32_t crcCount = 0;
  uint32_t errCount = 0;

  while (millis() - startedAt < windowMs) {
    if (!packetReceivedFlag) {
      delay(5);
      continue;
    }

    packetReceivedFlag = false;
    setLed(true);

    String payload;
    const int state = radio.readData(payload);
    const float rssi = radio.getRSSI();
    const float snr = radio.getSNR();
    const float freqErrHz = radio.getFrequencyError();
    const size_t packetLength = radio.getPacketLength();

    if (state == RADIOLIB_ERR_NONE) {
      okCount++;
      Serial.printf(
          "[%s] ok profile=%s len=%u rssi=%.2f snr=%.2f freqErr=%.0f Hz payload=%s\r\n",
          label,
          profile.name,
          static_cast<unsigned>(packetLength),
          rssi,
          snr,
          freqErrHz,
          isPrintablePayload(payload) ? payload.c_str() : toHexString(payload).c_str());
    } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
      crcCount++;
      Serial.printf(
          "[%s] crc profile=%s len=%u rssi=%.2f snr=%.2f freqErr=%.0f Hz\r\n",
          label,
          profile.name,
          static_cast<unsigned>(packetLength),
          rssi,
          snr,
          freqErrHz);
    } else {
      errCount++;
      Serial.printf(
          "[%s] err profile=%s state=%d len=%u rssi=%.2f snr=%.2f freqErr=%.0f Hz\r\n",
          label,
          profile.name,
          state,
          static_cast<unsigned>(packetLength),
          rssi,
          snr,
          freqErrHz);
    }

    setLed(false);
    startReceiveMode();
  }

  Serial.printf(
      "[%s] summary profile=%s ok=%lu crc=%lu err=%lu\r\n",
      label,
      profile.name,
      static_cast<unsigned long>(okCount),
      static_cast<unsigned long>(crcCount),
      static_cast<unsigned long>(errCount));
}

void runReceiveAcrossProfiles(uint32_t windowMsPerProfile) {
  for (const auto &profile : PROFILES) {
    runReceiveWindow(profile, windowMsPerProfile);
  }
}

void runBoostedReceiveWindow(const ScanProfile &profile, uint32_t windowMs) {
  RadioOptions options;
  options.boostedGain = true;
  options.crcEnabled = false;
  runReceiveWindow(profile, windowMs, options, "rx+gain");
}

void runSyncSweepWindow(const ScanProfile &profile, uint32_t windowMs) {
  RadioOptions options;
  options.boostedGain = true;
  options.crcEnabled = false;

  options.syncWord = SYNC_WORD_PRIVATE;
  runReceiveWindow(profile, windowMs, options, "sync-0x12");

  options.syncWord = SYNC_WORD_PUBLIC;
  runReceiveWindow(profile, windowMs, options, "sync-0x34");
}

void runTxPacket(const ScanProfile &profile, int8_t powerDbm, const char *tag) {
  if (!configureRadio(profile, powerDbm)) {
    return;
  }

  applyFemMode(FemMode::Tx);
  printFemState();

  String payload = String(tag) + " #" + String(++txCounter) + " @" + String(millis()) +
                   " " + profile.name + " " + String(powerDbm) + "dBm";

  Serial.printf("[tx] sending '%s'\r\n", payload.c_str());
  setLed(true);
  const uint32_t startedAt = millis();
  const int state = radio.transmit(payload);
  const uint32_t duration = millis() - startedAt;
  setLed(false);

  applyFemMode(FemMode::Rx);
  Serial.printf(
      "[tx] profile=%s power=%d state=%d duration=%lu ms\r\n",
      profile.name,
      powerDbm,
      state,
      static_cast<unsigned long>(duration));
}

void runTxBurst(const ScanProfile &profile, int8_t powerDbm, uint8_t count, uint16_t gapMs) {
  Serial.printf("[tx] burst profile=%s power=%d count=%u gap=%u\r\n",
                profile.name,
                powerDbm,
                count,
                gapMs);
  for (uint8_t i = 0; i < count; ++i) {
    runTxPacket(profile, powerDbm, "HELTEC_V4_DIAG_BURST");
    delay(gapMs);
  }
}

void runPowerSweep(const ScanProfile &profile) {
  Serial.printf("[sweep] power sweep on %s\r\n", profile.name);
  for (const int8_t powerDbm : POWER_SWEEP_DBM) {
    runTxPacket(profile, powerDbm, "HELTEC_V4_DIAG_PWR");
    delay(1200);
  }
}

void runContinuousWave(const ScanProfile &profile, int8_t powerDbm, uint32_t durationMs) {
  if (!configureRadio(profile, powerDbm)) {
    return;
  }

  applyFemMode(FemMode::Tx);
  printFemState();
  Serial.printf("[cw] start profile=%s power=%d duration=%lu ms\r\n",
                profile.name,
                powerDbm,
                static_cast<unsigned long>(durationMs));

  setLed(true);
  const uint32_t startedAt = millis();
  const int txState = radio.transmitDirect();
  if (txState != RADIOLIB_ERR_NONE) {
    printRadioState("transmitDirect", txState);
  } else {
    delay(durationMs);
  }

  const int standbyState = radio.standby();
  setLed(false);
  applyFemMode(FemMode::Rx);

  Serial.printf("[cw] done txState=%d standbyState=%d duration=%lu ms\r\n",
                txState,
                standbyState,
                static_cast<unsigned long>(millis() - startedAt));
}

void runFullCycle() {
  Serial.println("\r\n===== full diagnostic cycle =====");
  for (const auto &profile : PROFILES) {
    runCadWindow(profile, 5000);
  }
  runReceiveWindow(activeProfile(), 15000);
  runPowerSweep(activeProfile());
}

void printBanner() {
  Serial.println();
  Serial.println("Heltec V4 LoRa diagnostic bench");
  Serial.println();
  Serial.println("Profiles:");
  for (size_t i = 0; i < (sizeof(PROFILES) / sizeof(PROFILES[0])); ++i) {
    Serial.printf("  %u - %s\r\n", static_cast<unsigned>(i + 1), PROFILES[i].name);
  }
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  1/2/3 - select active profile");
  Serial.println("  c     - CAD scan all profiles");
  Serial.println("  r     - RX window on active profile (20s)");
  Serial.println("  a     - RX window on all profiles (12s each)");
  Serial.println("  g     - boosted RX on active profile, CRC off (20s)");
  Serial.println("  s     - sync sweep on active profile: 0x12 + 0x34");
  Serial.println("  t     - send one TX packet on active profile");
  Serial.println("  b     - send 5 TX packets on active profile");
  Serial.println("  p     - TX power sweep on active profile");
  Serial.println("  w     - continuous carrier on active profile (8s)");
  Serial.println("  x     - full diagnostic cycle");
  Serial.println("  i     - print current status and pin state");
  Serial.println("  h/?   - help");
  Serial.println();
  Serial.println("Notes:");
  Serial.println("  - Use this sketch when you want to test the RF path directly.");
  Serial.println("  - RX windows show real packets, RSSI/SNR, and frequency error.");
  Serial.println("  - Power sweep is useful with a nearby receiver or SDR.");
  Serial.println("  - Continuous carrier is the cleanest TX check for SDR/tinySA.");
  Serial.println("  - Sync sweep helps if the issue is in LoRa packet parameters.");
  Serial.println("  - Meshtastic compatibility is tested separately by the host script.");
  Serial.println();
}

void printStatus() {
  printActiveProfile();
  printPins();
}

void handleCommand(char cmd) {
  switch (cmd) {
    case '1':
    case '2':
    case '3':
      activeProfileIndex = static_cast<size_t>(cmd - '1');
      printActiveProfile();
      break;
    case 'a':
      runReceiveAcrossProfiles(12000);
      break;
    case 'b':
      runTxBurst(activeProfile(), DEFAULT_TX_POWER_DBM, 5, 1500);
      break;
    case 'c':
      for (const auto &profile : PROFILES) {
        runCadWindow(profile, 7000);
      }
      break;
    case 'g':
      runBoostedReceiveWindow(activeProfile(), 20000);
      break;
    case 'h':
    case '?':
      printBanner();
      break;
    case 'i':
      printStatus();
      break;
    case 'p':
      runPowerSweep(activeProfile());
      break;
    case 'r':
      runReceiveWindow(activeProfile(), 20000);
      break;
    case 's':
      runSyncSweepWindow(activeProfile(), 12000);
      break;
    case 't':
      runTxPacket(activeProfile(), DEFAULT_TX_POWER_DBM, "HELTEC_V4_DIAG_TX");
      break;
    case 'w':
      runContinuousWave(activeProfile(), DEFAULT_TX_POWER_DBM, 8000);
      break;
    case 'x':
      runFullCycle();
      break;
    default:
      break;
  }
}

void handleSerial() {
  while (Serial.available() > 0) {
    const char cmd = static_cast<char>(Serial.read());
    if (cmd == '\r' || cmd == '\n' || cmd == ' ') {
      continue;
    }
    handleCommand(static_cast<char>(tolower(cmd)));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(2000);

  configurePins();
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_CS);

  printBanner();
  printStatus();
  Serial.println("[boot] ready");
}

void loop() {
  handleSerial();
  delay(10);
}
