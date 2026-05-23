#include "runtime_support.h"

#include <cstring>

#include "ha_config.h"

bool mqttEndpointUsesWebSockets(const String& mqttHost) {
  return mqttHost.startsWith("ws://") ||
         mqttHost.startsWith("wss://") ||
         mqttHost.startsWith("http://") ||
         mqttHost.startsWith("https://");
}

bool mqttEndpointUsesSecureWebSockets(const String& mqttHost) {
  return mqttHost.startsWith("wss://") || mqttHost.startsWith("https://");
}

String normalizeWebSocketPath(const String& path) {
  if (path.length() == 0) {
    return String(HomeAssistantConfig::kMqttWebSocketPath);
  }

  return path[0] == '/' ? path : String("/") + path;
}

String stripUrlScheme(const String& value, const char* scheme) {
  return value.startsWith(scheme) ? value.substring(strlen(scheme)) : value;
}

bool isIpv4AddressLiteral(const String& value) {
  if (value.length() == 0) {
    return false;
  }

  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if ((c < '0' || c > '9') && c != '.') {
      return false;
    }
  }

  return true;
}

bool hasValue(const char* value) {
  return value != nullptr && value[0] != '\0';
}

String deviceIdentitySuffix() {
  uint64_t chipMac = ESP.getEfuseMac();
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06llx", chipMac & 0xFFFFFFULL);
  return String(suffix);
}

String defaultNodeId() {
  return String(HomeAssistantConfig::kNodeId) + "_" + deviceIdentitySuffix();
}

String defaultFriendlyName() {
  return String(HomeAssistantConfig::kFriendlyName) + " " + deviceIdentitySuffix();
}

String defaultRoomId() {
  return "room-default";
}

String defaultSensorRole() {
  return "auto";
}

bool usesFactoryDefaultIdentity(const String& value, const char* factoryDefault) {
  return value.length() == 0 || value == factoryDefault;
}

String sanitizeHostname(const String& value) {
  String hostname;
  hostname.reserve(value.length());

  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];

    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      hostname += c;
      continue;
    }

    if (c >= 'A' && c <= 'Z') {
      hostname += static_cast<char>(c - 'A' + 'a');
      continue;
    }

    if ((c == '-' || c == '_') && hostname.length() > 0 && hostname[hostname.length() - 1] != '-') {
      hostname += '-';
    }
  }

  while (hostname.startsWith("-")) {
    hostname.remove(0, 1);
  }
  while (hostname.endsWith("-")) {
    hostname.remove(hostname.length() - 1, 1);
  }

  if (hostname.length() == 0) {
    hostname = "lb-mmwave";
  }

  if (hostname.length() > 31) {
    hostname.remove(31);
  }

  return hostname;
}