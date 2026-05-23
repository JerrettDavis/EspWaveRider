#include "ble_support.h"

#include <PubSubClient.h>

#include "string_utils.h"

extern BleBeaconSighting bleSightings[MAX_BLE_SIGHTINGS];
extern BleIdentityTag bleIdentityTags[MAX_BLE_TAGS];
extern PubSubClient HomeAssistantMqttClient;

String homeAssistantStateTopic(const char* objectId);

static NimBLEScan* bleScanner = nullptr;

static void updateBleSighting(NimBLEAdvertisedDevice* advertisedDevice);

class BleScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* advertisedDevice) override {
    updateBleSighting(advertisedDevice);
  }
};

static BleScanCallbacks bleScanCallbacks;

uint8_t activeBleSightingCount() {
  uint32_t now = millis();
  uint8_t count = 0;
  for (uint8_t i = 0; i < MAX_BLE_SIGHTINGS; i++) {
    if (bleSightings[i].occupied && (now - bleSightings[i].lastSeenMs) <= BLE_SIGHTING_FRESHNESS_MS) {
      count++;
    }
  }
  return count;
}

String normalizeBleIdentityValue(const String& value) {
  String normalized;
  normalized.reserve(value.length());

  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c >= 'A' && c <= 'F') {
      normalized += static_cast<char>(c - 'A' + 'a');
      continue;
    }
    if ((c >= 'a' && c <= 'f') || (c >= '0' && c <= '9')) {
      normalized += c;
    }
  }

  return normalized;
}

void clearBleIdentityTag(uint8_t slot) {
  if (slot >= MAX_BLE_TAGS) {
    return;
  }

  bleIdentityTags[slot].occupied = false;
  bleIdentityTags[slot].label = "";
  bleIdentityTags[slot].address = "";
  bleIdentityTags[slot].minRssi = BLE_TAG_DEFAULT_MIN_RSSI;
  bleIdentityTags[slot].lastRssi = -127;
  bleIdentityTags[slot].lastSeenMs = 0;
}

bool bleIdentityTagPresent(const BleIdentityTag& tag, uint32_t now) {
  return tag.occupied && (now - tag.lastSeenMs) <= BLE_TAG_FRESHNESS_MS;
}

uint8_t activeBleTagCount() {
  uint32_t now = millis();
  uint8_t count = 0;
  for (uint8_t i = 0; i < MAX_BLE_TAGS; i++) {
    if (bleIdentityTagPresent(bleIdentityTags[i], now)) {
      count++;
    }
  }
  return count;
}

void appendBleSightingsJson(String& json) {
  json += ",\"ble_beacon_count\":" + String(activeBleSightingCount());
  json += ",\"ble_beacons\":[";

  bool first = true;
  uint32_t now = millis();
  for (uint8_t i = 0; i < MAX_BLE_SIGHTINGS; i++) {
    BleBeaconSighting& sighting = bleSightings[i];
    if (!sighting.occupied || (now - sighting.lastSeenMs) > BLE_SIGHTING_FRESHNESS_MS) {
      continue;
    }

    if (!first) {
      json += ",";
    }
    first = false;

    json += "{\"address\":\"" + jsonEscape(sighting.address) + "\"";
    json += ",\"name\":\"" + jsonEscape(sighting.name) + "\"";
    json += ",\"service_uuid\":\"" + jsonEscape(sighting.serviceUuid) + "\"";
    json += ",\"rssi\":" + String(sighting.rssi);
    json += ",\"age_ms\":" + String(now - sighting.lastSeenMs);
    json += "}";
  }

  json += "]";
}

void appendBleTagsJson(String& json) {
  const uint32_t now = millis();
  json += ",\"ble_tagged_people_count\":" + String(activeBleTagCount());
  json += ",\"ble_tags\":[";

  bool first = true;
  for (uint8_t i = 0; i < MAX_BLE_TAGS; i++) {
    const BleIdentityTag& tag = bleIdentityTags[i];
    if (!tag.occupied) {
      continue;
    }

    if (!first) {
      json += ",";
    }
    first = false;

    json += "{\"slot\":" + String(i);
    json += ",\"label\":\"" + jsonEscape(tag.label) + "\"";
    json += ",\"address\":\"" + jsonEscape(tag.address) + "\"";
    json += ",\"min_rssi\":" + String(tag.minRssi);
    json += ",\"present\":" + String(bleIdentityTagPresent(tag, now) ? "true" : "false");
    json += ",\"rssi\":" + String(tag.lastRssi);
    json += ",\"age_ms\":" + String(tag.lastSeenMs > 0 ? (now - tag.lastSeenMs) : BLE_TAG_FRESHNESS_MS + 1UL);
    json += "}";
  }

  json += "]";
}

static void updateBleSighting(NimBLEAdvertisedDevice* advertisedDevice) {
  if (advertisedDevice == nullptr) {
    return;
  }

  String address = String(advertisedDevice->getAddress().toString().c_str());
  if (address.length() == 0) {
    return;
  }

  int slot = -1;
  for (uint8_t i = 0; i < MAX_BLE_SIGHTINGS; i++) {
    if (bleSightings[i].occupied && bleSightings[i].address == address) {
      slot = i;
      break;
    }
    if (!bleSightings[i].occupied && slot < 0) {
      slot = i;
    }
  }

  if (slot < 0) {
    slot = 0;
    for (uint8_t i = 1; i < MAX_BLE_SIGHTINGS; i++) {
      if (bleSightings[i].lastSeenMs < bleSightings[slot].lastSeenMs) {
        slot = i;
      }
    }
  }

  bleSightings[slot].occupied = true;
  bleSightings[slot].address = address;
  bleSightings[slot].name = advertisedDevice->haveName() ? String(advertisedDevice->getName().c_str()) : "";
  bleSightings[slot].serviceUuid = advertisedDevice->haveServiceUUID() ? String(advertisedDevice->getServiceUUID().toString().c_str()) : "";
  bleSightings[slot].rssi = advertisedDevice->getRSSI();
  bleSightings[slot].lastSeenMs = millis();

  const String normalizedAddress = normalizeBleIdentityValue(address);
  for (uint8_t tagIndex = 0; tagIndex < MAX_BLE_TAGS; tagIndex++) {
    BleIdentityTag& tag = bleIdentityTags[tagIndex];
    if (!tag.occupied || tag.address != normalizedAddress) {
      continue;
    }

    if (bleSightings[slot].rssi < tag.minRssi) {
      continue;
    }

    tag.lastRssi = bleSightings[slot].rssi;
    tag.lastSeenMs = bleSightings[slot].lastSeenMs;
  }
}

void initializeBleScanner() {
  NimBLEDevice::init("");
  bleScanner = NimBLEDevice::getScan();
  if (bleScanner == nullptr) {
    return;
  }

  bleScanner->setAdvertisedDeviceCallbacks(&bleScanCallbacks, false);
  bleScanner->setActiveScan(true);
  bleScanner->setInterval(90);
  bleScanner->setWindow(45);
  bleScanner->setDuplicateFilter(false);
  bleScanner->setMaxResults(0);
}

void ensureBleScannerActive() {
  if (bleScanner == nullptr || bleScanner->isScanning()) {
    return;
  }

  bleScanner->start(BLE_SCAN_WINDOW_MS, nullptr, false);
}

void publishBleStates() {
  if (!HomeAssistantMqttClient.connected()) {
    return;
  }

  const uint32_t now = millis();
  char numberBuffer[16];

  snprintf(numberBuffer, sizeof(numberBuffer), "%u", activeBleSightingCount());
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("ble_beacon_count").c_str(), numberBuffer, true);

  String payload = "[";
  bool first = true;
  for (uint8_t i = 0; i < MAX_BLE_SIGHTINGS; i++) {
    BleBeaconSighting& sighting = bleSightings[i];
    if (!sighting.occupied || (now - sighting.lastSeenMs) > BLE_SIGHTING_FRESHNESS_MS) {
      continue;
    }
    if (!first) {
      payload += ",";
    }
    first = false;
    payload += "{\"address\":\"" + jsonEscape(sighting.address) + "\"";
    payload += ",\"name\":\"" + jsonEscape(sighting.name) + "\"";
    payload += ",\"service_uuid\":\"" + jsonEscape(sighting.serviceUuid) + "\"";
    payload += ",\"rssi\":" + String(sighting.rssi);
    payload += "}";
  }
  payload += "]";
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("ble_beacons_json").c_str(), payload.c_str(), true);

  snprintf(numberBuffer, sizeof(numberBuffer), "%u", activeBleTagCount());
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("ble_tagged_people_count").c_str(), numberBuffer, true);

  for (uint8_t tagIndex = 0; tagIndex < MAX_BLE_TAGS; tagIndex++) {
    const BleIdentityTag& tag = bleIdentityTags[tagIndex];
    char trackerObjectId[24];
    char rssiObjectId[24];
    snprintf(trackerObjectId, sizeof(trackerObjectId), "ble_tag_%02u_tracker", tagIndex);
    snprintf(rssiObjectId, sizeof(rssiObjectId), "ble_tag_%02u_rssi", tagIndex);

    if (!tag.occupied) {
      HomeAssistantMqttClient.publish(homeAssistantStateTopic(trackerObjectId).c_str(), "not_home", true);
      HomeAssistantMqttClient.publish(homeAssistantStateTopic(rssiObjectId).c_str(), "-127", true);
      continue;
    }

    const bool present = bleIdentityTagPresent(tag, now);
    HomeAssistantMqttClient.publish(homeAssistantStateTopic(trackerObjectId).c_str(), present ? "home" : "not_home", true);

    char rssiPayload[16];
    snprintf(rssiPayload, sizeof(rssiPayload), "%d", present ? tag.lastRssi : -127);
    HomeAssistantMqttClient.publish(homeAssistantStateTopic(rssiObjectId).c_str(), rssiPayload, true);
  }
}