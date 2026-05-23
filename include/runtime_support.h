#pragma once

#include <Arduino.h>

bool mqttEndpointUsesWebSockets(const String& mqttHost);
bool mqttEndpointUsesSecureWebSockets(const String& mqttHost);
String normalizeWebSocketPath(const String& path);
String stripUrlScheme(const String& value, const char* scheme);
bool isIpv4AddressLiteral(const String& value);
bool hasValue(const char* value);
String deviceIdentitySuffix();
String defaultNodeId();
String defaultFriendlyName();
String defaultRoomId();
String defaultSensorRole();
bool usesFactoryDefaultIdentity(const String& value, const char* factoryDefault);
String sanitizeHostname(const String& value);