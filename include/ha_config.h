#pragma once

#if __has_include("ha_secrets.h")
#include "ha_secrets.h"
#endif

#ifndef HA_ENABLED
#define HA_ENABLED 1
#endif

#ifndef HA_WIFI_SSID
#define HA_WIFI_SSID ""
#endif

#ifndef HA_WIFI_PASSWORD
#define HA_WIFI_PASSWORD ""
#endif

#ifndef HA_MQTT_HOST
#define HA_MQTT_HOST ""
#endif

#ifndef HA_MQTT_PORT
#define HA_MQTT_PORT 1883
#endif

#ifndef HA_MQTT_USERNAME
#define HA_MQTT_USERNAME ""
#endif

#ifndef HA_MQTT_PASSWORD
#define HA_MQTT_PASSWORD ""
#endif

#ifndef HA_MQTT_USE_WEBSOCKETS
#define HA_MQTT_USE_WEBSOCKETS 0
#endif

#ifndef HA_MQTT_WEBSOCKET_PATH
#define HA_MQTT_WEBSOCKET_PATH "/mqtt"
#endif

#ifndef HA_MQTT_HOST_HEADER
#define HA_MQTT_HOST_HEADER ""
#endif

#ifndef HA_NODE_ID
#define HA_NODE_ID "lb_mmwave_presence"
#endif

#ifndef HA_FRIENDLY_NAME
#define HA_FRIENDLY_NAME "LB mmWave Presence"
#endif

#ifndef HA_DISCOVERY_PREFIX
#define HA_DISCOVERY_PREFIX "homeassistant"
#endif

#ifndef HA_TOPIC_PREFIX
#define HA_TOPIC_PREFIX "lb_mmwave"
#endif

namespace HomeAssistantConfig {
static constexpr bool kEnabled = HA_ENABLED != 0;
static constexpr const char* kWifiSsid = HA_WIFI_SSID;
static constexpr const char* kWifiPassword = HA_WIFI_PASSWORD;
static constexpr const char* kMqttHost = HA_MQTT_HOST;
static constexpr uint16_t kMqttPort = HA_MQTT_PORT;
static constexpr const char* kMqttUsername = HA_MQTT_USERNAME;
static constexpr const char* kMqttPassword = HA_MQTT_PASSWORD;
static constexpr bool kMqttUseWebSockets = HA_MQTT_USE_WEBSOCKETS != 0;
static constexpr const char* kMqttWebSocketPath = HA_MQTT_WEBSOCKET_PATH;
static constexpr const char* kMqttHostHeader = HA_MQTT_HOST_HEADER;
static constexpr const char* kNodeId = HA_NODE_ID;
static constexpr const char* kFriendlyName = HA_FRIENDLY_NAME;
static constexpr const char* kDiscoveryPrefix = HA_DISCOVERY_PREFIX;
static constexpr const char* kTopicPrefix = HA_TOPIC_PREFIX;
}