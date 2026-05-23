#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>

class WebSocketMqttClient : public Client {
 public:
  WebSocketMqttClient();

  void configure(const String& host, uint16_t port, const String& path, const String& hostHeader);
  void loop();

  int connect(IPAddress ip, uint16_t port) override;
  int connect(const char* host, uint16_t port) override;
  size_t write(uint8_t value) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  int available() override;
  int read() override;
  int read(uint8_t* buffer, size_t size) override;
  int peek() override;
  void flush() override;
  void stop() override;
  uint8_t connected() override;
  operator bool() override;

 private:
  static constexpr uint32_t kConnectTimeoutMs = 5000;
  static constexpr size_t kRxBufferSize = 2048;
  static constexpr size_t kTxBufferSize = 1024;

  void handleEvent(WStype_t type, uint8_t* payload, size_t length);
  void appendRx(const uint8_t* payload, size_t length);
  bool flushPendingFrame();

  WebSocketsClient webSocketClient;
  String configuredHost;
  uint16_t configuredPort = 80;
  String configuredPath = "/mqtt";
  String configuredHostHeader;
  String extraHeaders;
  bool socketConnected = false;
  bool connectFailed = false;
  uint8_t rxBuffer[kRxBufferSize] = {0};
  size_t rxLength = 0;
  uint8_t txBuffer[kTxBufferSize] = {0};
  size_t txLength = 0;
};