#include "device_web.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#include "generated_visualizer_page.h"

extern DNSServer DeviceDnsServer;
extern WebServer DeviceWebServer;
extern WebSocketsServer DeviceWebSocket;
extern bool webServerStarted;
extern bool webSocketServerStarted;
extern bool dnsServerStarted;

String buildDeviceSnapshotJson(int32_t energySince = -1,
                               int32_t textSince = -1,
                               int32_t genericSince = -1);
String buildDebugStatusJson();
void handleUsbCommand(const String& command);

void handleHttpCommand() {
  String command = DeviceWebServer.arg("plain");
  command.trim();
  if (command == "debug_status") {
    DeviceWebServer.send(200, "application/json", buildDebugStatusJson());
    return;
  }
  if (command.length() > 0) {
    handleUsbCommand(command);
  }
  DeviceWebServer.send(200, "application/json", buildDeviceSnapshotJson());
}

void broadcastDeviceSnapshot() {
  if (!webSocketServerStarted) {
    return;
  }

  String payload = buildDeviceSnapshotJson();
  DeviceWebSocket.broadcastTXT(payload);
}

void ensureDeviceWebSocketActive() {
  if (webSocketServerStarted) {
    return;
  }

  DeviceWebSocket.begin();
  DeviceWebSocket.onEvent([](uint8_t clientId, WStype_t type, uint8_t* payload, size_t length) {
    (void)payload;
    (void)length;

    if (type == WStype_CONNECTED) {
      String payload = buildDeviceSnapshotJson();
      DeviceWebSocket.sendTXT(clientId, payload);
    }
  });

  webSocketServerStarted = true;
}

void ensureDeviceWebServerActive() {
  if (webServerStarted) {
    return;
  }

  DeviceWebServer.on("/", HTTP_GET, []() {
    DeviceWebServer.send_P(200, "text/html; charset=utf-8", kEmbeddedVisualizerPage);
  });
  DeviceWebServer.on("/api/snapshot", HTTP_GET, []() {
    int32_t energySince = DeviceWebServer.hasArg("energy_since") ? DeviceWebServer.arg("energy_since").toInt() : -1;
    int32_t textSince = DeviceWebServer.hasArg("text_since") ? DeviceWebServer.arg("text_since").toInt() : -1;
    int32_t genericSince = DeviceWebServer.hasArg("generic_since") ? DeviceWebServer.arg("generic_since").toInt() : -1;
    DeviceWebServer.send(200, "application/json", buildDeviceSnapshotJson(energySince, textSince, genericSince));
  });
  DeviceWebServer.on("/api/command", HTTP_POST, handleHttpCommand);
  DeviceWebServer.onNotFound([]() {
    DeviceWebServer.sendHeader("Location", "/");
    DeviceWebServer.send(302, "text/plain", "Redirecting");
  });

  DeviceWebServer.begin();
  webServerStarted = true;
}

void handleDeviceWebServer() {
  if (dnsServerStarted) {
    DeviceDnsServer.processNextRequest();
  }

  if (webServerStarted) {
    DeviceWebServer.handleClient();
  }

  if (webSocketServerStarted) {
    DeviceWebSocket.loop();
  }
}