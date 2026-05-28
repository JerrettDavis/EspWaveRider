#include "string_utils.h"

String buildHexString(const uint8_t* data, size_t length) {
  static constexpr char kHexDigits[] = "0123456789ABCDEF";
  String hex;
  hex.reserve(length * 2);

  for (size_t i = 0; i < length; i++) {
    uint8_t value = data[i];
    hex += kHexDigits[(value >> 4) & 0x0F];
    hex += kHexDigits[value & 0x0F];
  }

  return hex;
}

String jsonEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];

    switch (c) {
      case '"': escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\b': escaped += "\\b"; break;
      case '\f': escaped += "\\f"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if (static_cast<uint8_t>(c) < 0x20) {
          escaped += '?';
        } else {
          escaped += c;
        }
        break;
    }
  }

  return escaped;
}

String percentDecode(const String& value) {
  String decoded;
  decoded.reserve(value.length());

  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];

    if (c == '%' && (i + 2) < value.length()) {
      char high = value[i + 1];
      char low = value[i + 2];
      auto hexValue = [](char hex) -> int {
        if (hex >= '0' && hex <= '9') return hex - '0';
        if (hex >= 'a' && hex <= 'f') return 10 + (hex - 'a');
        if (hex >= 'A' && hex <= 'F') return 10 + (hex - 'A');
        return -1;
      };

      int highValue = hexValue(high);
      int lowValue = hexValue(low);

      if (highValue >= 0 && lowValue >= 0) {
        decoded += static_cast<char>((highValue << 4) | lowValue);
        i += 2;
        continue;
      }
    }

    decoded += (c == '+') ? ' ' : c;
  }

  return decoded;
}

bool splitConfigFields(const String& payload, String* fields, size_t expectedFieldCount) {
  size_t fieldIndex = 0;
  int start = 0;

  while (fieldIndex < expectedFieldCount) {
    int separator = payload.indexOf('|', start);

    if (separator < 0) {
      fields[fieldIndex++] = percentDecode(payload.substring(start));
      break;
    }

    fields[fieldIndex++] = percentDecode(payload.substring(start, separator));
    start = separator + 1;
  }

  while (fieldIndex < expectedFieldCount) {
    fields[fieldIndex++] = "";
  }

  return true;
}

String jsonFieldString(const String& json, const char* fieldName) {
  String key = String("\"") + fieldName + "\":";
  int keyIndex = json.indexOf(key);
  if (keyIndex < 0) {
    return "";
  }

  int valueIndex = keyIndex + key.length();
  if (valueIndex >= json.length()) {
    return "";
  }

  if (json[valueIndex] == '"') {
    valueIndex++;
    String value;
    value.reserve(32);

    for (int i = valueIndex; i < json.length(); i++) {
      char c = json[i];
      if (c == '"') {
        return value;
      }
      if (c == '\\' && (i + 1) < json.length()) {
        char escaped = json[++i];
        switch (escaped) {
          case 'n': value += '\n'; break;
          case 'r': value += '\r'; break;
          case 't': value += '\t'; break;
          case '\\': value += '\\'; break;
          case '"': value += '"'; break;
          default: value += escaped; break;
        }
        continue;
      }
      value += c;
    }

    return value;
  }

  int endIndex = json.indexOf(',', valueIndex);
  if (endIndex < 0) {
    endIndex = json.indexOf('}', valueIndex);
  }
  if (endIndex < 0) {
    endIndex = json.length();
  }

  return json.substring(valueIndex, endIndex);
}

int jsonFieldInt(const String& json, const char* fieldName, int fallbackValue) {
  String value = jsonFieldString(json, fieldName);
  if (value.length() == 0 || value == "null") {
    return fallbackValue;
  }
  return value.toInt();
}

bool jsonFieldBool(const String& json, const char* fieldName, bool fallbackValue) {
  String value = jsonFieldString(json, fieldName);
  if (value == "true") {
    return true;
  }
  if (value == "false") {
    return false;
  }
  return fallbackValue;
}