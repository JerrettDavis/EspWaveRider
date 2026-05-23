#include "websocket_mqtt_client.h"

#include <cstring>

WebSocketMqttClient::WebSocketMqttClient() {
  webSocketClient.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
    handleEvent(type, payload, length);
  });
  webSocketClient.setReconnectInterval(0);
}

void WebSocketMqttClient::configure(const String& host, uint16_t port, const String& path, const String& hostHeader) {
  configuredHost = host;
  configuredPort = port;
  configuredPath = path.length() > 0 ? path : "/mqtt";
  configuredHostHeader = hostHeader;
}

void WebSocketMqttClient::loop() {
  flush();
  webSocketClient.loop();
}

int WebSocketMqttClient::connect(IPAddress ip, uint16_t port) {
  return connect(ip.toString().c_str(), port);
}

int WebSocketMqttClient::connect(const char* host, uint16_t port) {
  stop();

  configuredHost = host != nullptr ? String(host) : configuredHost;
  configuredPort = port;
  connectFailed = false;
  socketConnected = false;

  extraHeaders = "";
  if (configuredHostHeader.length() > 0) {
    extraHeaders = String("Host: ") + configuredHostHeader + "\r\n";
    webSocketClient.setExtraHeaders(extraHeaders.c_str());
  } else {
    webSocketClient.setExtraHeaders(nullptr);
  }

  webSocketClient.begin(configuredHost.c_str(), configuredPort, configuredPath.c_str(), "mqtt");

  uint32_t startedAt = millis();
  while (!socketConnected && !connectFailed && (millis() - startedAt) < kConnectTimeoutMs) {
    webSocketClient.loop();
    delay(10);
  }

  return socketConnected ? 1 : 0;
}

size_t WebSocketMqttClient::write(uint8_t value) {
  if (txLength >= sizeof(txBuffer)) {
    if (!flushPendingFrame()) {
      return 0;
    }
  }

  txBuffer[txLength++] = value;
  return 1;
}

size_t WebSocketMqttClient::write(const uint8_t* buffer, size_t size) {
  if (buffer == nullptr || size == 0) {
    return 0;
  }

  if (txLength > 0 && (txLength + size) <= sizeof(txBuffer)) {
    memcpy(txBuffer + txLength, buffer, size);
    txLength += size;
    return size;
  }

  if (txLength > 0 && !flushPendingFrame()) {
    return 0;
  }

  if (size <= sizeof(txBuffer)) {
    memcpy(txBuffer, buffer, size);
    txLength = size;
    return size;
  }

  return webSocketClient.sendBIN(buffer, size) ? size : 0;
}

int WebSocketMqttClient::available() {
  flush();
  webSocketClient.loop();
  return static_cast<int>(rxLength);
}

int WebSocketMqttClient::read() {
  uint8_t value = 0;
  return read(&value, 1) == 1 ? value : -1;
}

int WebSocketMqttClient::read(uint8_t* buffer, size_t size) {
  if (buffer == nullptr || size == 0 || rxLength == 0) {
    return 0;
  }

  size_t bytesToCopy = rxLength < size ? rxLength : size;
  memcpy(buffer, rxBuffer, bytesToCopy);
  rxLength -= bytesToCopy;

  if (rxLength > 0) {
    memmove(rxBuffer, rxBuffer + bytesToCopy, rxLength);
  }

  return static_cast<int>(bytesToCopy);
}

int WebSocketMqttClient::peek() {
  return rxLength > 0 ? rxBuffer[0] : -1;
}

void WebSocketMqttClient::flush() {
  flushPendingFrame();
}

void WebSocketMqttClient::stop() {
  txLength = 0;
  rxLength = 0;
  socketConnected = false;
  connectFailed = false;
  webSocketClient.disconnect();
}

uint8_t WebSocketMqttClient::connected() {
  return socketConnected ? 1 : 0;
}

WebSocketMqttClient::operator bool() {
  return connected() != 0;
}

void WebSocketMqttClient::handleEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      socketConnected = true;
      connectFailed = false;
      break;
    case WStype_BIN:
      appendRx(payload, length);
      break;
    case WStype_DISCONNECTED:
      socketConnected = false;
      connectFailed = true;
      break;
    case WStype_ERROR:
      socketConnected = false;
      connectFailed = true;
      break;
    default:
      break;
  }
}

void WebSocketMqttClient::appendRx(const uint8_t* payload, size_t length) {
  if (payload == nullptr || length == 0) {
    return;
  }

  size_t bytesToCopy = length;
  if (bytesToCopy > sizeof(rxBuffer)) {
    payload += (bytesToCopy - sizeof(rxBuffer));
    bytesToCopy = sizeof(rxBuffer);
  }

  if ((rxLength + bytesToCopy) > sizeof(rxBuffer)) {
    size_t overflow = (rxLength + bytesToCopy) - sizeof(rxBuffer);
    if (overflow >= rxLength) {
      rxLength = 0;
    } else {
      memmove(rxBuffer, rxBuffer + overflow, rxLength - overflow);
      rxLength -= overflow;
    }
  }

  memcpy(rxBuffer + rxLength, payload, bytesToCopy);
  rxLength += bytesToCopy;
}

bool WebSocketMqttClient::flushPendingFrame() {
  if (txLength == 0) {
    return true;
  }

  if (!socketConnected) {
    return false;
  }

  bool sent = webSocketClient.sendBIN(txBuffer, txLength);
  if (sent) {
    txLength = 0;
  }

  return sent;
}