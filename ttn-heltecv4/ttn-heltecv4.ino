/*
  Heltec WiFi LoRa 32 V4 + TTN EU868 OTAA + OLED status/boot screen

  Uses the community "Heltec_ESP32_LoRa_v3" (heltec_unofficial.h) board
  library + RadioLib's LoRaWAN stack instead of Heltec's own official
  LoRaWan_APP library. Heltec's official library gates the radio behind a
  per-chip "license" check (visible on serial as "Please provide a correct
  license!") that blocks Mcu.begin() from ever returning on unlicensed
  boards - this stack talks to the SX1262 directly via RadioLib and has no
  such gate. V3 and V4 boards share identical OLED/radio/Vext pin mappings
  (verified against this Arduino core's pins_arduino.h for both variants),
  so the V3-branded library works fine here.

  1) Copy secrets.h.example to secrets.h and fill in your TTN OTAA
     credentials (secrets.h is gitignored, never committed)
  2) Select "Heltec WiFi LoRa 32(V4)" board, any USB Mode/CDC On Boot
     setting works since we no longer depend on Heltec's own USB stack
     quirks for this
  3) Upload and open Serial Monitor @115200
*/

#include <heltec_unofficial.h>   // Serial/Vext/OLED/radio init - provides `radio`, `display`, `both`
#include "secrets.h"

// -------------------- LoRaWAN Region --------------------
const LoRaWANBand_t Region = EU868;
const uint8_t subBand = 0;

// -------------------- OTAA keys (from secrets.h, not committed) --------------------
uint64_t joinEUI = SECRET_JOIN_EUI;
uint64_t devEUI  = SECRET_DEV_EUI;
// This device is registered as LoRaWAN 1.1 (separate AppKey/NwkKey in TTN),
// so both are used as-is below rather than reusing AppKey for both, as a
// 1.0.x device would need.
uint8_t *appKey = SECRET_APP_KEY;
uint8_t *nwkKey = SECRET_NWK_KEY;

LoRaWANNode node(&radio, &Region, subBand);

// -------------------- LoRaWAN behavior --------------------
uint8_t appPort = 2;

// Send interval (ms) - keep fair use in mind
uint32_t appTxDutyCycle = 120000;

// -------------------- App payload --------------------
static uint32_t uplinkCount = 0;

// Simple 32x32 "meshpoint" style bitmap placeholder (XBM format)
// You can replace this with your own logo.
static const uint8_t logo32x32[] PROGMEM = {
  0x00,0x00,0x00,0x00,0xFC,0x0F,0x00,0x00,0x02,0x10,0x00,0x00,0xF1,0x23,0x00,0x00,
  0x09,0x24,0x00,0x00,0x09,0x24,0x00,0x00,0xF1,0x23,0x00,0x00,0x02,0x10,0x00,0x00,
  0xFC,0x0F,0x00,0x00,0x00,0x00,0x00,0x00,0xFC,0x0F,0x00,0x00,0x02,0x10,0x00,0x00,
  0xF1,0x23,0x00,0x00,0x09,0x24,0x00,0x00,0x09,0x24,0x00,0x00,0xF1,0x23,0x00,0x00,
  0x02,0x10,0x00,0x00,0xFC,0x0F,0x00,0x00,0x00,0x00,0x00,0x00,0xFC,0x0F,0x00,0x00,
  0x02,0x10,0x00,0x00,0xF1,0x23,0x00,0x00,0x09,0x24,0x00,0x00,0x09,0x24,0x00,0x00,
  0xF1,0x23,0x00,0x00,0x02,0x10,0x00,0x00,0xFC,0x0F,0x00,0x00,0x00,0x00,0x00,0x00
};

void oledShowBootLogo()
{
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);

  // Draw logo centered roughly at top/middle
  display.drawXbm(48, 4, 32, 32, logo32x32);

  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 40, "TTN Node PD2EMC");
  display.drawString(64, 52, "Heltec V4 EU868");
  display.display();
  delay(1800);
}

void oledStatus(const String &line1, const String &line2 = "", const String &line3 = "")
{
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0,  line1);
  if (line2.length()) display.drawString(0, 14, line2);
  if (line3.length()) display.drawString(0, 28, line3);

  // footer
  display.drawString(0, 52, "Uplinks: " + String(uplinkCount));
  display.display();
}

static void printHex(const uint8_t *buf, size_t len)
{
  for (size_t i = 0; i < len; i++) Serial.printf("%02X", buf[i]);
}

// Build payload
static void prepareTxFrame(uint8_t *buf, uint8_t &len)
{
  static uint8_t counter = 0;
  len = 2;
  buf[0] = counter++;
  buf[1] = (uint8_t)random(0, 255);
}

// The RADIOLIB()/RADIOLIB_OR_HALT() macros from heltec_unofficial.h only
// decode a handful of generic radio codes, so LoRaWAN-specific ones (like
// "no join accept") fall through to a bare lookup-table URL. This fills in
// the LoRaWAN codes so the log/OLED actually say what happened.
static String lorawanStateDecode(int16_t state)
{
  switch (state) {
    case RADIOLIB_ERR_NONE:
      return "OK";
    case RADIOLIB_LORAWAN_NEW_SESSION:
      return "Joined (new session)";
    case RADIOLIB_LORAWAN_SESSION_RESTORED:
      return "Joined (session restored)";
    case RADIOLIB_ERR_NETWORK_NOT_JOINED:
      return "Not joined yet";
    case RADIOLIB_ERR_NO_JOIN_ACCEPT:
      return "No join-accept - request sent, nothing answered "
             "(device not registered on the network, or no gateway "
             "reachable/forwarding to it)";
    case RADIOLIB_ERR_MIC_MISMATCH:
      return "MIC mismatch - AppKey/NwkKey likely wrong, or LoRaWAN "
             "version (1.0.x vs 1.1) doesn't match TTN's device setting";
    case RADIOLIB_ERR_DOWNLINK_MALFORMED:
      return "Downlink malformed";
    case RADIOLIB_ERR_INVALID_REVISION:
      return "Invalid LoRaWAN revision";
    case RADIOLIB_ERR_INVALID_PORT:
      return "Invalid fPort";
    case RADIOLIB_ERR_NO_RX_WINDOW:
      return "No RX window open";
    case RADIOLIB_ERR_INVALID_CID:
      return "Invalid MAC command CID";
    case RADIOLIB_ERR_UPLINK_UNAVAILABLE:
      return "Uplink unavailable (duty cycle/dwell time limit)";
    case RADIOLIB_ERR_COMMAND_QUEUE_FULL:
      return "MAC command queue full";
    case RADIOLIB_ERR_COMMAND_QUEUE_ITEM_NOT_FOUND:
      return "MAC command queue item not found";
    case RADIOLIB_ERR_JOIN_NONCE_INVALID:
      return "Join nonce invalid (try re-joining; some networks require "
             "\"Resets DevNonces\" enabled on the device)";
    case RADIOLIB_ERR_DWELL_TIME_EXCEEDED:
      return "Dwell time exceeded";
    case RADIOLIB_ERR_CHECKSUM_MISMATCH:
      return "Checksum mismatch";
    case RADIOLIB_ERR_NONCES_DISCARDED:
      return "Nonces discarded, session cleared - re-join required";
    case RADIOLIB_ERR_SESSION_DISCARDED:
      return "Session discarded - re-join required";
    case RADIOLIB_ERR_CHIP_NOT_FOUND:
      return "Radio chip not found - check wiring/board selection";
    case RADIOLIB_ERR_RX_TIMEOUT:
      return "RX timeout";
    case RADIOLIB_ERR_INVALID_FREQUENCY:
      return "Invalid frequency";
  }
  return "See https://jgromes.github.io/RadioLib/group__status__codes.html";
}

void setup()
{
  // heltec_display_power() (called from heltec_setup() below) only enables
  // Vext for the "Wireless Stick" board variant - it assumes the regular
  // V3 board doesn't gate the OLED through Vext. Our V4 board does (same
  // as the official Heltec library needed), so we have to power it
  // ourselves before display.init() runs inside heltec_setup(), or the
  // screen stays dark even though I2C calls silently "succeed".
  heltec_ve(true);
  delay(50);

  heltec_setup();   // Serial.begin(115200) + SPI + OLED init/reset
  delay(300);

  oledShowBootLogo();
  oledStatus("Booting...", "Init radio");

  Serial.println("\n===================================");
  Serial.println("Heltec V4 TTN EU868 OTAA + OLED");
  Serial.println("===================================");
  Serial.printf("DevEUI:  %016llX\n", devEUI);
  Serial.printf("JoinEUI: %016llX\n", joinEUI);
  Serial.print("AppKey:  "); printHex(appKey, 16); Serial.println();
  Serial.print("NwkKey:  "); printHex(nwkKey, 16); Serial.println();
  Serial.printf("TX duty cycle: %lu ms\n", (unsigned long)appTxDutyCycle);
  Serial.println("===================================\n");

  Serial.println("STATE: INIT radio");
  RADIOLIB_OR_HALT(radio.begin());

  RADIOLIB(node.beginOTAA(joinEUI, devEUI, nwkKey, appKey));

  oledStatus("STATE: JOIN", "Joining TTN...", "OTAA");
  int16_t state;
  uint8_t attempt = 0;
  do {
    attempt++;
    Serial.printf("STATE: JOIN (attempt %u)\n", attempt);
    state = node.activateOTAA();
    RADIOLIB(state);
    Serial.println(lorawanStateDecode(state));
    if (state != RADIOLIB_LORAWAN_NEW_SESSION && state != RADIOLIB_LORAWAN_SESSION_RESTORED) {
      // Join-requests count against duty-cycle/fair-use limits just like
      // uplinks, so retries wait the same appTxDutyCycle interval.
      uint32_t waitStart = millis();
      while (millis() - waitStart < appTxDutyCycle) {
        uint32_t remaining = appTxDutyCycle - (millis() - waitStart);
        oledStatus("STATE: JOIN", "Join failed (#" + String(attempt) + ")",
                   "Retry in " + String(remaining / 1000) + "s");
        delay(1000);
      }
    }
  } while (state != RADIOLIB_LORAWAN_NEW_SESSION && state != RADIOLIB_LORAWAN_SESSION_RESTORED);

  Serial.print("Joined. DevAddr: 0x");
  Serial.println((unsigned long)node.getDevAddr(), HEX);
  oledStatus("STATE: JOIN", "Joined!", "DevAddr set");
  delay(1000);
}

void loop()
{
  Serial.println("STATE: SEND");

  uint8_t payload[2];
  uint8_t payloadLen;
  prepareTxFrame(payload, payloadLen);

  Serial.printf("Payload (%d bytes): ", payloadLen);
  printHex(payload, payloadLen);
  Serial.println();

  uint8_t downlink[256];
  size_t downlinkLen = sizeof(downlink);

  int16_t state = node.sendReceive(payload, payloadLen, appPort, downlink, &downlinkLen);
  RADIOLIB(state);
  Serial.println(lorawanStateDecode(state));

  if (state > 0) {
    Serial.println("---- Downlink received ----");
    Serial.printf("  Size:  %d bytes\n", downlinkLen);
    if (downlinkLen > 0) {
      Serial.print("  Data:  ");
      printHex(downlink, downlinkLen);
      Serial.println();
    }
    Serial.println("----------------------------");
  } else if (state == RADIOLIB_ERR_NONE) {
    Serial.println("Uplink sent, no downlink.");
  } else {
    Serial.println("Uplink failed.");
  }

  uplinkCount++;
  oledStatus("STATE: SEND", "Uplink #" + String(uplinkCount), "fPort: " + String(appPort));
  Serial.printf("Uplink #%lu done\n", (unsigned long)uplinkCount);

  uint32_t waitMs = appTxDutyCycle + random(-3000, 3000);
  uint32_t waitStart = millis();
  Serial.printf("STATE: CYCLE, next in %lu ms\n", (unsigned long)waitMs);
  while (millis() - waitStart < waitMs) {
    uint32_t remaining = waitMs - (millis() - waitStart);
    oledStatus("STATE: CYCLE", "Next tx in:", String(remaining / 1000) + " s");
    delay(1000);
  }
}
