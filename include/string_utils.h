#pragma once

#include <Arduino.h>

String buildHexString(const uint8_t* data, size_t length);
String jsonEscape(const String& value);
String percentDecode(const String& value);
bool splitConfigFields(const String& payload, String* fields, size_t expectedFieldCount);
String jsonFieldString(const String& json, const char* fieldName);
int jsonFieldInt(const String& json, const char* fieldName, int fallbackValue);
bool jsonFieldBool(const String& json, const char* fieldName, bool fallbackValue);