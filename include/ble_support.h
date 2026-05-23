#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

static constexpr uint8_t MAX_BLE_SIGHTINGS = 16;
static constexpr uint8_t MAX_BLE_TAGS = 8;
static constexpr uint32_t BLE_SIGHTING_FRESHNESS_MS = 30000;
static constexpr uint32_t BLE_TAG_FRESHNESS_MS = 45000;
static constexpr uint32_t BLE_SCAN_WINDOW_MS = 0;
static constexpr int BLE_TAG_DEFAULT_MIN_RSSI = -88;

struct BleBeaconSighting {
  bool occupied = false;
  String address;
  String name;
  String serviceUuid;
  int rssi = -127;
  uint32_t lastSeenMs = 0;
};

struct BleIdentityTag {
  bool occupied = false;
  String label;
  String address;
  int minRssi = BLE_TAG_DEFAULT_MIN_RSSI;
  int lastRssi = -127;
  uint32_t lastSeenMs = 0;
};

void appendBleSightingsJson(String& json);
void appendBleTagsJson(String& json);
void initializeBleScanner();
void ensureBleScannerActive();
uint8_t activeBleSightingCount();
uint8_t activeBleTagCount();
void publishBleStates();
String normalizeBleIdentityValue(const String& value);
void clearBleIdentityTag(uint8_t slot);
bool bleIdentityTagPresent(const BleIdentityTag& tag, uint32_t now);