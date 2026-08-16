#ifndef TTN_HELTECV3_SECRETS_H
#define TTN_HELTECV3_SECRETS_H

// Copy this file to secrets.h and fill in your own TTN OTAA credentials.
// secrets.h is gitignored - it will not be committed.

#define SECRET_JOIN_EUI 0x0000000000000000
#define SECRET_DEV_EUI  0x0000000000000000

// Paste straight from the TTN console's key field - 32 hex chars. Spaces/
// dashes in between are fine too, they get stripped when the .ino parses
// this into the byte array RadioLib actually wants.
#define SECRET_APP_KEY "00000000000000000000000000000000"
// Only needed for LoRaWAN 1.1 devices (separate AppKey/NwkKey in TTN
// console). For 1.0.x devices, set this to the same string as SECRET_APP_KEY.
#define SECRET_NWK_KEY "00000000000000000000000000000000"

// Wifi status page / OTA (only used when running on USB, see WiFi
// auto-detect in the .ino).
#define SECRET_WIFI_SSID     "TechInc"
#define SECRET_WIFI_PASSWORD "itoldyoualready"

// Fallback access point, started only if the WiFi STA connect above fails.
#define SECRET_AP_SSID     "ttn-heltecv3-setup"
#define SECRET_AP_PASSWORD "itoldyoualready"

// Status page / OTA login password
#define SECRET_WEB_PASSWORD "itoldyoualready"

#endif
