#include <Arduino.h>
#include <HTTPClient.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <NimBLEDevice.h>
#include <Update.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <Adafruit_NeoPixel.h>
#include <mbedtls/sha256.h>
#include <cstring>
#include <time.h>

#ifndef ESPWAVERIDER_FIRMWARE_VERSION
#define ESPWAVERIDER_FIRMWARE_VERSION "dev"
#endif

#ifndef ESPWAVERIDER_BUILD_TARGET
#define ESPWAVERIDER_BUILD_TARGET "unknown"
#endif

#ifndef ESPWAVERIDER_GIT_SHA
#define ESPWAVERIDER_GIT_SHA "unknown"
#endif

#ifndef ESPWAVERIDER_USB_CONSOLE
#define ESPWAVERIDER_USB_CONSOLE 1
#endif

#include "board_profile.h"
#include "ble_support.h"
#include "device_web.h"
#include "generated_visualizer_page.h"
#include "ha_config.h"
#include "runtime_support.h"
#include "string_utils.h"
#include "websocket_mqtt_client.h"

// =====================================================
// Lonely Binary ESP32-S3 + HLK-LD2420 telemetry firmware
//
// USB Serial:
//   - Emits newline-delimited JSON for easy JS capture.
//   - 115200 baud.
//
// Radar UART:
//   - LD2420 OT1 -> ESP GPIO4
//   - LD2420 RX  -> ESP GPIO5
//
// Presence GPIO:
//   - LD2420 OT2 -> ESP GPIO6
//
// Notes:
//   - This firmware attempts to place the LD2420 into energy output mode.
//   - If the module keeps outputting plain text like "ON\r\nRange 22\r\n",
//     the app can still show presence/range, but you will not get 16 gate values.
// =====================================================

static constexpr uint32_t USB_BAUD = kBoardProfile.usbBaud;
static constexpr uint32_t RADAR_BAUD = kBoardProfile.radarBaud;

static constexpr int RADAR_RX_PIN = kBoardProfile.radarRxPin;
static constexpr int RADAR_TX_PIN = kBoardProfile.radarTxPin;
static constexpr int RADAR_PRESENCE_PIN = kBoardProfile.radarPresencePin;
static constexpr uint8_t RADAR_PRESENCE_PIN_MODE = kBoardProfile.radarPresencePinMode;

static constexpr uint32_t HEARTBEAT_MS = 1000;
static constexpr uint32_t PRESENCE_POLL_MS = 25;
static constexpr uint32_t RADAR_IDLE_FRAME_GAP_MS = 20;
static constexpr size_t RADAR_FRAME_BUFFER_SIZE = 256;
static constexpr uint32_t WIFI_RETRY_MS = 10000;
static constexpr uint32_t MQTT_RETRY_MS = 5000;
static constexpr byte AP_DNS_PORT = 53;
static constexpr uint16_t DEVICE_HTTP_PORT = 80;
static constexpr uint16_t DEVICE_WS_PORT = 81;
static constexpr uint8_t MAX_WIFI_SCAN_RESULTS = 16;
static constexpr uint8_t MAX_UDP_DISCOVERY_PEERS = 12;
static constexpr uint16_t UDP_DISCOVERY_PORT = 42110;
static constexpr uint32_t UDP_DISCOVERY_ANNOUNCE_MS = 5000;
static constexpr uint32_t UDP_DISCOVERY_PEER_FRESHNESS_MS = 20000;
static constexpr uint8_t LD2420_GATE_COUNT = 16;
static constexpr uint16_t LD2420_GATE_SIZE_CM = 70;
static constexpr uint16_t LD2420_ACTIVE_GATE_FLOOR = 25;
static constexpr uint8_t LD2420_MAX_ESTIMATED_PEOPLE = 4;
static constexpr uint8_t LD2420_NEAR_FIELD_CLUTTER_MAX_GATE_INDEX = 1;
static constexpr uint16_t LD2420_NEAR_FIELD_CLUTTER_DISTANCE_DELTA_CM = 105;
static constexpr uint8_t LD2420_NEAR_FIELD_CLUTTER_PEAK_SHARE_PERCENT = 45;
static constexpr uint8_t LD2420_NEAR_FIELD_CLUTTER_BAND_GATES = 3;
static constexpr uint8_t LD2420_NEAR_FIELD_CLUTTER_BAND_SHARE_PERCENT = 55;
static constexpr uint8_t MAX_ROOM_PEERS = 8;
static constexpr uint32_t ROOM_PEER_FRESHNESS_MS = 15000;
static constexpr uint16_t ROOM_DISTANCE_SEPARATION_CM = 140;
static constexpr uint16_t ROOM_FUSION_CLUSTER_RADIUS_CM = 110;
static constexpr uint16_t ROOM_SENSOR_GHOST_RADIUS_CM = 80;
static constexpr uint16_t ROOM_FUSION_ROOM_MARGIN_CM = 80;
static constexpr uint16_t ROOM_RELATIVE_POSE_SPACING_CM = 15;
static constexpr uint16_t ROOM_ANGLE_MOTION_THRESHOLD_CM = 12;
static constexpr int8_t ROOM_ANGLE_SCORE_LIMIT = 12;
static constexpr int STATUS_RGB_LED_PIN = kBoardProfile.statusRgbLedPin;
static constexpr uint8_t STATUS_RGB_LED_COUNT = kBoardProfile.statusRgbLedCount;
static constexpr uint8_t DEFAULT_LED_BRIGHTNESS = 32;
static constexpr uint16_t DEFAULT_PRESENCE_HOLD_MS = 4000;
static constexpr uint16_t MIN_PRESENCE_HOLD_MS = 3000;
static constexpr uint16_t MAX_PRESENCE_HOLD_MS = 60000;
static constexpr uint32_t ROOM_SUMMARY_KEEPALIVE_MS = 10000;
static constexpr uint16_t ROOM_SUMMARY_DISTANCE_DELTA_CM = 35;
static constexpr uint8_t ROOM_SUMMARY_ACTIVITY_DELTA = 6;
static constexpr const char* FIRMWARE_RELEASE_REPO_OWNER = "JerrettDavis";
static constexpr const char* FIRMWARE_RELEASE_REPO_NAME = "EspWaveRider";
static constexpr size_t FIRMWARE_DOWNLOAD_BUFFER_SIZE = 4096;
static constexpr time_t MIN_TRUSTED_TLS_UNIX_TIME = 1704067200;
static constexpr uint32_t FIRMWARE_TLS_TIME_SYNC_WAIT_MS = 15000;

static constexpr const char* FIRMWARE_RELEASE_TRUST_ANCHORS = R"PEM(-----BEGIN CERTIFICATE-----
MIIDXzCCAuagAwIBAgIQNuBZ7YiN1Xrt1XC2cn+b2jAKBggqhkjOPQQDAzBfMQswCQYD
VQQGEwJHQjEYMBYGA1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1TZWN0aWdv
IFB1YmxpYyBTZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBFNDYwHhcNMjEwMzIyMDAw
MDAwWhcNMzYwMzIxMjM1OTU5WjBgMQswCQYDVQQGEwJHQjEYMBYGA1UEChMPU2VjdGln
byBMaW1pdGVkMTcwNQYDVQQDEy5TZWN0aWdvIFB1YmxpYyBTZXJ2ZXIgQXV0aGVudGlj
YXRpb24gQ0EgRFYgRTM2MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEaKGnbAUnBYlj
HDmn/yUhxe3TLxKYuyzc9VXoSaCEV5F73Fhfa/Si/RMsmwTFW3R9s7J6JpYZFmu4do3v
k/Vgl6OCAYEwggF9MB8GA1UdIwQYMBaAFNEi2kxZ8UtfJjiqndbu6w3D+6lhMB0GA1Ud
DgQWBBQXmagEwW/kLXCoChA9A9PpGrgmYzAOBgNVHQ8BAf8EBAMCAYYwEgYDVR0TAQH/
BAgwBgEB/wIBADAdBgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwGwYDVR0gBBQw
EjAGBgRVHSAAMAgGBmeBDAECATBUBgNVHR8ETTBLMEmgR6BFhkNodHRwOi8vY3JsLnNl
Y3RpZ28uY29tL1NlY3RpZ29QdWJsaWNTZXJ2ZXJBdXRoZW50aWNhdGlvblJvb3RFNDYu
Y3JsMIGEBggrBgEFBQcBAQR4MHYwTwYIKwYBBQUHMAKGQ2h0dHA6Ly9jcnQuc2VjdGln
by5jb20vU2VjdGlnb1B1YmxpY1NlcnZlckF1dGhlbnRpY2F0aW9uUm9vdEU0Ni5wN2Mw
IwYIKwYBBQUHMAGGF2h0dHA6Ly9vY3NwLnNlY3RpZ28uY29tMAoGCCqGSM49BAMDA2cA
MGQCMFsKnBQDh64l+v+aUYWjDCJKQMxHUUGmcwAYDIjJ9pbRYItMCIx5xu0oUb6sIfTX
qQIwPddcsDE4KdeLu1hJdpHgdLvsHAK3vygyLGujMU9xBJCDackRT93VHEE0gppgNqdV
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICOjCCAcGgAwIBAgIQQvLM2htpN0RfFf51KBC49DAKBggqhkjOPQQDAzBfMQswCQYD
VQQGEwJHQjEYMBYGA1UEChMPU2VjdGlnbyBMaW1pdGVkMTYwNAYDVQQDEy1TZWN0aWdv
IFB1YmxpYyBTZXJ2ZXIgQXV0aGVudGljYXRpb24gUm9vdCBFNDYwHhcNMjEwMzIyMDAw
MDAwWhcNNDYwMzIxMjM1OTU5WjBfMQswCQYDVQQGEwJHQjEYMBYGA1UEChMPU2VjdGln
byBMaW1pdGVkMTYwNAYDVQQDEy1TZWN0aWdvIFB1YmxpYyBTZXJ2ZXIgQXV0aGVudGlj
YXRpb24gUm9vdCBFNDYwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAR2+pmpbiDt+dd34wc7
qNs9Xzjoq1WmVk/WSOrsfy2qw7LFeeyZYX8QeccCWvkEN/U0NSt3zn8gj1KjAIns1aei
bVvjS5KToID1AZTc8GgHHs3u/iVStSBDHBv+6xnOQ6OjQjBAMB0GA1UdDgQWBBTRItpM
WfFLXyY4qp3W7usNw/upYTAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAK
BggqhkjOPQQDAwNnADBkAjAn7qRaqCG76UeXlImldCBteU/IvZNeWBj7LRoAasm4PdCk
T0RHlAFWovgzJQxC36oCMB3q4S6ILuH5px0CMk7yn2xVdOOurvulGu7t0vzCAxHrRVxgE
D1cf5kDW21USAGKcw==
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIFBjCCAu6gAwIBAgIRAMISMktwqbSRcdxA9+KFJjwwDQYJKoZIhvcNAQELBQAwTzEL
MAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2VhcmNoIEdy
b3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMjQwMzEzMDAwMDAwWhcNMjcwMzEy
MjM1OTU5WjAzMQswCQYDVQQGEwJVUzEWMBQGA1UEChMNTGV0J3MgRW5jcnlwdDEMMAoG
A1UEAxMDUjEyMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA2pgodK2+lP47
4B7i5Ut1qywSf+2nAzJ+Npfs6DGPpRONC5kuHs0BUT1M5ShuCVUxqqUiXXL0LQfCTUA8
3wEjuXg39RplMjTmhnGdBO+ECFu9AhqZ66YBAJpzkG2Pogeg0JfT2kVhgTU9FPnEwF9q
3AuWGrCf4yrqvSrWmMebcas7dA8827JgvlpLThjp2ypzXIlhZZ7+7Tymy05v5J75AEaz
/xlNKmOzjmbGGIVwx1Blbzt05UiDDwhYXS0jnV6j/ujbAKHS9OMZTfLuevYnnuXNnC2i
8n+cF63vEzc50bTILEHWhsDp7CH4WRt/uTp8n1wBnWIEwii9Cq08yhDsGwIDAQABo4H4
MIH1MA4GA1UdDwEB/wQEAwIBhjAdBgNVHSUEFjAUBggrBgEFBQcDAgYIKwYBBQUHAwEw
EgYDVR0TAQH/BAgwBgEB/wIBADAdBgNVHQ4EFgQUALUp8i2ObzHom0yteD763OkM0dIw
HwYDVR0jBBgwFoAUebRZ5nu25eQBc4AIiMgaWPbpm24wMgYIKwYBBQUHAQEEJjAkMCIG
CCsGAQUFBzAChhZodHRwOi8veDEuaS5sZW5jci5vcmcvMBMGA1UdIAQMMAowCAYGZ4EM
AQIBMCcGA1UdHwQgMB4wHKAaoBiGFmh0dHA6Ly94MS5jLmxlbmNyLm9yZy8wDQYJKoZI
hvcNAQELBQADggIBAI910AnPanZIZTKS3rVEyIV29BWEjAK/duuz8eL5boSoVpHhkkv3
4eoAeEiPdZLj5EZ7G2ArIK+gzhTlRQ1q4FKGpPPaFBSpqV/xbUb5UlAXQOnkHn3mFVj+
qYv87/WeY+Bm4sN3Ox8BhyaU7UAQ3LeZ7N1X01xxQe4wIAAE3JVLUCiHmZL+qoCUtgYI
FPgcg350QMUIWgxPXNGEncT921ne7nluI02V8pLUmClqXOsCwULw+PVOZCB7qOMxxMBo
CUeL2Ll4oMpOSr5pJCpLN3tRA2s6P1KLs9TSrVhOk+7LX28NMUlIusQ/nxLJID0RhAeF
tPjyOCOscQBA53+NRjSCak7P4A5jX7ppmkcJECL+S0i3kXVUy5Me5BbrU8973jZNv/ax
6+ZK6TM8jWmimL6of6OrX7ZU6E2WqazzsFrLG3o2kySbzlhSgJ81Cl4tv3SbYiYXnJEx
KQvzf83DYotox3f0fwv7xln1A2ZLplCb0O+l/AK0YE0DS2FPxSAHi0iwMfW2nNHJrXcY
3LLHD77gRgje4Eveubi2xxa+Nmk/hmhLdIETiVDFanoCrMVIpQ59XWHkzdFmoHXHBV7o
ibVjGSO7ULSQ7MJ1Nz51phuDJSgAIU7A0zrLnOrAj/dfrlEWRhCvAgbuwLZX1A2sjNjX
oPOHbsPiy+lO1KF8/XY7
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAwTzEL
MAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2VhcmNoIEdy
b3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4WhcNMzUwNjA0
MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkg
UmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMTCCAiIwDQYJKoZIhvcN
AQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygch77ct984kIxuPOZXoHj3dcKi
/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+0TM8ukj13Xnfs7j/EvEhmkvBioZx
aUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6UA5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0t
tov0DiNewNwIRt18jA8+o+u3dpjq+sWT8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4
VMk7BPZ7hm/ELNKjD+Jo2FR3qyHB5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxd
AQ4Q7e2RCOFvu396j3x+UCB5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6aw
BdpUKD9jf1b0SHzUvKBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEada
o0xAH0ahmbWnOlfuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk
3SzynTnjh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwr
bwqHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CIrU
7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8E
BTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkqhkiG9w0BAQsF
AAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZLubhzEFnTIZd+50xx
+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ3BebYhtF8GaV0nxvwuo7
7x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KKNFtY2PwByVS5uCbMiogziUwt
hDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5ORAzI4JMPJ+GslWYHb4phowim57i
aztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7UrTkXWStAmzOVyyghqpZXjFaH3pO3JLF+l
/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdCjNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+
63SM1N95R1NbdWhscdCb+ZAJzVcoyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4U
i0/1lvh+wjChP4kqKOJ2qxq4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0
GE44Za4rF2LN9d11TPAmRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4
r7g1SgEEzwxA57demyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1An
X5iItreGCc=
-----END CERTIFICATE-----
)PEM";

static constexpr uint8_t ENERGY_HEADER[] = {0xF4, 0xF3, 0xF2, 0xF1};
static constexpr uint8_t ENERGY_FOOTER[] = {0xF8, 0xF7, 0xF6, 0xF5};
static constexpr size_t ENERGY_FRAME_LENGTH = 45;
static constexpr uint32_t RUNTIME_BENCHMARK_ITERATIONS = 1000;
static constexpr const char* RUNTIME_BENCHMARK_SIMPLE_COMMAND = "runtime_benchmark";
static constexpr const char* RUNTIME_BENCHMARK_ROOM_CONFIG_PAYLOAD = "room-default|auto|50|25|-90|800|400";
static constexpr const char* RUNTIME_BENCHMARK_TUNING_CONFIG_PAYLOAD = "0|0|500|-1|0|0|on|0";
static constexpr const char* RUNTIME_BENCHMARK_GENERIC_FRAME_HEX = "110012000D000D000D00F8F7F6F5F4F3F2F123000169005257F113DD006801280014003A0014000D000A002000140011000D000D000A00F8F7F6F5F4F3F2F12300016900754AA511C901DD0048006400120049000D00140014001900110014000D000D00F8F7F6F5F4F3F2F12300016900A1414510610152001A001400120019001400110019001400140011000A001400F8F7F6F5F4F3F2F12300016900444B241328015A0028001D0050002D001A002900110014000D00110022001100F8F7F6F5";
static constexpr uint16_t RUNTIME_BENCHMARK_ENERGY_GATES[LD2420_GATE_COUNT] = {12898, 3730, 362, 36, 80, 98, 20, 13, 13, 9, 10, 13, 20, 10, 13, 10};

HardwareSerial RadarSerial(kBoardProfile.radarSerialPort);
WiFiClient HomeAssistantWiFiClient;
WiFiUDP DeviceUdpDiscovery;
WebSocketMqttClient HomeAssistantWebSocketClient;
PubSubClient HomeAssistantMqttClient(HomeAssistantWiFiClient);
DNSServer DeviceDnsServer;
WebServer DeviceWebServer(DEVICE_HTTP_PORT);
WebSocketsServer DeviceWebSocket(DEVICE_WS_PORT);
Preferences SettingsStore;
Adafruit_NeoPixel StatusRgbLed(STATUS_RGB_LED_COUNT > 0 ? STATUS_RGB_LED_COUNT : 1,
                               STATUS_RGB_LED_PIN >= 0 ? STATUS_RGB_LED_PIN : 0,
                               NEO_GRB + NEO_KHZ800);

struct WiFiScanNetwork {
  String ssid;
  String bssid;
  String authMode;
  int32_t rssi = 0;
  int32_t channel = 0;
  bool open = false;
};

struct UdpDiscoveryPeer {
  bool occupied = false;
  String nodeId;
  String friendlyName;
  String roomId;
  String sensorRole;
  String firmwareVersion;
  String buildTarget;
  String hostname;
  String ipAddress;
  int32_t wifiRssi = 0;
  int32_t wifiChannel = 0;
  uint32_t uptimeSeconds = 0;
  uint32_t freeHeapBytes = 0;
  uint32_t lastSeenMs = 0;
};

struct LatestEnergyFrameSnapshot {
  bool valid = false;
  size_t length = 0;
  uint16_t payloadLength = 0;
  bool presence = false;
  uint16_t distanceCm = 0;
  uint16_t gates[LD2420_GATE_COUNT] = {0};
  uint32_t bytesTotal = 0;
  uint32_t framesTotal = 0;
  uint32_t energyFramesTotal = 0;
};

struct RadarDerivedMetrics {
  bool valid = false;
  bool energyBased = false;
  uint8_t estimatedPeople = 0;
  uint8_t activeGateCount = 0;
  int dominantGateIndex = -1;
  int dominantGateDistanceCm = -1;
  uint16_t dominantGateEnergy = 0;
  uint32_t totalGateEnergy = 0;
  uint8_t activityScore = 0;
};

struct LatestTextFrameSnapshot {
  bool valid = false;
  size_t length = 0;
  bool presence = false;
  int range = -1;
  uint32_t bytesTotal = 0;
  uint32_t framesTotal = 0;
  String hex;
  String ascii;
};

struct LatestGenericFrameSnapshot {
  bool valid = false;
  size_t length = 0;
  uint32_t bytesTotal = 0;
  uint32_t framesTotal = 0;
  String hex;
  String ascii;
};

struct RuntimeBenchmarkMeasurement {
  uint32_t totalUs = 0;
  uint32_t perIterNs = 0;
};

struct RuntimeBenchmarkSnapshot {
  bool valid = false;
  uint32_t measuredAtMs = 0;
  uint32_t iterations = 0;
  RuntimeBenchmarkMeasurement parseCommandFixture;
  RuntimeBenchmarkMeasurement parseRoomConfigFixture;
  RuntimeBenchmarkMeasurement parseTuningConfigFixture;
  RuntimeBenchmarkMeasurement parseGenericFixture;
  RuntimeBenchmarkMeasurement deriveMetricsFixture;
  RuntimeBenchmarkMeasurement detectionCandidateFixture;
  bool detectionCandidate = false;
  uint8_t peopleEstimate = 0;
  uint8_t activeGateCount = 0;
  uint8_t activityScore = 0;
  int dominantGateDistanceCm = -1;
};

enum class RadarDetectionDecision : uint8_t {
  Candidate = 0,
  InvalidMetrics,
  OutOfRange,
  InsufficientActiveGates,
  LowActivity,
  MissingEnergyFrame,
  LowEnergy,
  NearFieldClutter,
};

struct RuntimeHomeAssistantConfig {
  bool enabled = false;
  String wifiSsid;
  String wifiPassword;
  String mqttHost;
  uint16_t mqttPort = 1883;
  String mqttUsername;
  String mqttPassword;
  bool mqttUseWebSockets = false;
  String mqttWebSocketPath;
  String mqttHostHeader;
  String nodeId;
  String friendlyName;
  String roomId;
  String sensorRole;
  int16_t roomPoseXCm = 0;
  int16_t roomPoseYCm = 0;
  int16_t roomHeadingDeg = -90;
  uint16_t roomWidthCm = 600;
  uint16_t roomHeightCm = 400;
  uint16_t maxDetectionRangeCm = LD2420_GATE_COUNT * LD2420_GATE_SIZE_CM;
  uint16_t minGateEnergy = LD2420_ACTIVE_GATE_FLOOR;
  uint8_t sensitivityPercent = 55;
  uint16_t presenceHoldMs = DEFAULT_PRESENCE_HOLD_MS;
  uint8_t minActiveGates = 1;
  uint8_t minActivityScore = 10;
  bool ledEnabled = true;
  uint8_t ledBrightness = DEFAULT_LED_BRIGHTNESS;
};

struct RoomPeerSummary {
  bool occupied = false;
  String nodeId;
  String sensorRole;
  String firmwareVersion;
  String buildTarget;
  bool presence = false;
  bool detectionCandidate = false;
  uint8_t peopleEstimate = 0;
  uint8_t activeGateCount = 0;
  int dominantGateDistanceCm = -1;
  uint8_t activityScore = 0;
  int16_t poseXCm = 0;
  int16_t poseYCm = 0;
  int16_t headingDeg = -90;
  uint16_t roomWidthCm = 600;
  uint16_t roomHeightCm = 400;
  int relativeAngleGuessDeg = 0;
  uint8_t relativeAngleConfidencePercent = 0;
  int8_t parallelScore = 0;
  int8_t perpendicularScore = 0;
  int lastObservedDistanceCm = -1;
  uint32_t lastUpdatedMs = 0;
};

struct PublishedRoomSummaryState {
  bool valid = false;
  bool presence = false;
  bool detectionCandidate = false;
  uint8_t peopleEstimate = 0;
  uint8_t activeGateCount = 0;
  int dominantGateDistanceCm = -1;
  uint8_t activityScore = 0;
  int16_t poseXCm = 0;
  int16_t poseYCm = 0;
  int16_t headingDeg = -90;
  uint16_t roomWidthCm = 600;
  uint16_t roomHeightCm = 400;
  uint32_t publishedAtMs = 0;
};

struct RoomAggregateMetrics {
  uint8_t peopleEstimate = 0;
  uint8_t activeNodeCount = 0;
  uint8_t peerNodeCount = 0;
  uint8_t activityScore = 0;
};

struct FirmwareSyncState {
  bool pending = false;
  bool inProgress = false;
  bool lastSuccess = false;
  String targetVersion;
  String targetNodeId;
  String targetSource;
  String downloadUrl;
  String statusText;
  String lastError;
  uint32_t lastStartedMs = 0;
  uint32_t lastCompletedMs = 0;
};

RuntimeHomeAssistantConfig runtimeConfig;
LatestEnergyFrameSnapshot latestEnergyFrameSnapshot;
LatestTextFrameSnapshot latestTextFrameSnapshot;
LatestGenericFrameSnapshot latestGenericFrameSnapshot;
RoomPeerSummary roomPeers[MAX_ROOM_PEERS];
BleBeaconSighting bleSightings[MAX_BLE_SIGHTINGS];
BleIdentityTag bleIdentityTags[MAX_BLE_TAGS];
WiFiScanNetwork lastWiFiScanResults[MAX_WIFI_SCAN_RESULTS];
UdpDiscoveryPeer udpDiscoveryPeers[MAX_UDP_DISCOVERY_PEERS];
uint8_t lastWiFiScanResultCount = 0;

uint32_t bootMillis = 0;
uint32_t lastHeartbeatMs = 0;
uint32_t lastPresencePollMs = 0;
uint32_t lastRadarByteMs = 0;

bool lastPresence = false;
bool lastGpioPresence = false;
bool detectionCandidateActive = false;
bool presenceInitialized = false;
bool homeAssistantDiscoveryPublished = false;
int lastLocalDominantGateDistanceCm = -1;
PublishedRoomSummaryState lastPublishedRoomSummary;

uint8_t radarBuffer[RADAR_FRAME_BUFFER_SIZE];
size_t radarBufferLength = 0;

uint32_t radarBytesTotal = 0;
uint32_t radarFramesTotal = 0;
uint32_t ld2420EnergyFramesTotal = 0;
uint32_t presenceChangesTotal = 0;
uint32_t lastDetectionMs = 0;
uint32_t lastWiFiConnectAttemptMs = 0;
uint32_t lastMqttConnectAttemptMs = 0;

int lastDistanceCm = -1;
int lastWiFiDisconnectReason = 0;
String lastWiFiDisconnectReasonText = "idle";
String lastKnownIpAddress = "0.0.0.0";
String lastResolvedMqttHost = "unresolved";
int lastMqttState = -1;
String lastMqttStateText = "DISCONNECTED";
String lastSoftApIpAddress = "0.0.0.0";
String lastSoftApSsid;
String lastSoftApPassword;
String lastDeviceHostname = "lb-mmwave";
bool webServerStarted = false;
bool webSocketServerStarted = false;
bool dnsServerStarted = false;
bool mdnsStarted = false;
bool udpDiscoveryStarted = false;
FirmwareSyncState firmwareSyncState;
RuntimeBenchmarkSnapshot latestRuntimeBenchmarkSnapshot;

String usbCommandBuffer;
uint32_t lastWiFiScanMs = 0;
uint32_t wifiScansTotal = 0;
uint32_t lastUdpDiscoveryAnnounceMs = 0;

// -----------------------------------------------------
// Forward declarations
// -----------------------------------------------------

void printJsonString(const char* value);
void printJsonString(const String& value);
void printHexByte(uint8_t value);
void printJsonEventPrefix(const char* eventType);
void finishJsonEvent();

void emitBootEvent();
void emitHeartbeatEvent();
void emitPresenceEvent(bool presence, bool changed);
void emitRadarFrameEvent(const uint8_t* data, size_t length);
void emitCommandEvent(const String& command);
void emitErrorEvent(const char* message);
void emitTextRangeFrame(const uint8_t* data, size_t length, const String& ascii);

bool startsWithEnergyHeader(const uint8_t* data, size_t len);
bool endsWithEnergyFooter(const uint8_t* data, size_t len);
uint16_t readLe16(const uint8_t* data, size_t offset);
size_t decodeHexBytes(const char* hex, uint8_t* output, size_t capacity);
void buildGenericFrameSnapshot(const uint8_t* data, size_t length, LatestGenericFrameSnapshot& snapshot);
bool parseLd2420EnergyFrame(const uint8_t* data, size_t len, LatestEnergyFrameSnapshot& snapshot);
bool tryEmitLd2420EnergyFrame(const uint8_t* data, size_t len);
bool tryEmitLd2420TextFrame(const uint8_t* data, size_t len);
bool tryEmitEmbeddedLd2420EnergyFrames(const uint8_t* data, size_t len, size_t& consumedLength);
uint8_t classifyBenchmarkCommand(const String& command);
uint32_t parseBenchmarkRoomConfigPayload(const char* payload);
uint32_t parseBenchmarkTuningConfigPayload(const char* payload);
void runRuntimeBenchmark();

void flushRadarBufferIfNeeded(bool force);
void readRadarUart();
void pollPresence();
void handleUsbCommand(const String& command);
void readUsbCommands();
void writeRadarCommand(const uint8_t* data, size_t len, const char* name);
void configureLd2420EnergyMode();
void loadHomeAssistantConfig();
void saveHomeAssistantConfig();
void resetHomeAssistantConnections();
void emitHomeAssistantConfigEvent();
void emitWiFiScanResults();
void serviceUdpDiscovery();
void announceUdpDiscovery(bool emitEvent);
bool homeAssistantConfigured();
String sanitizeHostname(const String& value);
String deviceHostname();
String deviceHostnameLabel();
String deviceDashboardUrl();
const char* firmwareVersion();
const char* firmwareBuildTarget();
const char* firmwareGitSha();
String semanticVersionCore(const String& version);
int compareSemanticVersions(const String& leftVersion, const String& rightVersion);
bool findHighestPeerReleaseVersion(String& nodeId, String& version, String& source);
String firmwareReleaseAssetUrl(const String& versionCore);
String firmwareReleaseAssetName(const String& versionCore, const char* extension);
String firmwareReleaseApiUrl(const String& versionCore);
bool beginTrustedFirmwareRequest(HTTPClient& http, WiFiClientSecure& client, const String& url, String& error);
bool ensureTrustedTlsClock(String& error);
String extractSha256Hex(const String& value);
String fetchFirmwareReleaseChecksum(const String& versionCore, String& error);
bool downloadAndApplyFirmware(const String& downloadUrl, const String& expectedSha256, String& error);
void appendFirmwareSyncJson(String& json);
void requestFirmwareUpdate(const String& targetVersion, const String& targetNodeId, const String& targetSource);
void serviceFirmwareSync();
bool peerVersionMismatch(const String& peerVersion);
void emitPeerVersionEvent(const char* source, const String& nodeId, const String& peerVersion, bool mismatch);
String accessPointSsid();
String accessPointPassword();
void ensureAccessPointActive();
void ensureMdnsActive();
String buildDeviceSnapshotJson(int32_t energySince = -1,
                               int32_t textSince = -1,
                               int32_t genericSince = -1);
String buildDebugStatusJson();
String defaultRoomId();
String defaultSensorRole();
String roomTopicRoot();
String roomNodeSummaryTopic(const String& nodeId);
String roomSummarySubscriptionTopic();
String roomPoseCommandTopic(const String& nodeId);
String roomPoseCommandSubscriptionTopic();
void handleRoomSummaryMessage(const String& topic, const String& payload);
void applyRoomPoseConfig(const String& roomId,
                         const String& sensorRole,
                         const String& poseXValue,
                         const String& poseYValue,
                         const String& headingValue,
                         const String& roomWidthValue,
                         const String& roomHeightValue,
                         bool resetConnections,
                         bool emitSavedEvent,
                         const char* eventName);
void handleRoomPoseCommandMessage(const String& payload);
bool publishRoomPoseCommand(const String& nodeId,
                            const String& roomId,
                            const String& sensorRole,
                            int16_t poseXCm,
                            int16_t poseYCm,
                            int16_t headingDeg,
                            uint16_t roomWidthCm,
                            uint16_t roomHeightCm);
void updateRoomPeerAngleHint(RoomPeerSummary& peer, int localDominantGateDistanceCm, int peerDominantGateDistanceCm);
void appendRoomPeersJson(String& json, int localDominantGateDistanceCm);
void appendUdpDiscoveryPeersJson(String& json);
void handleMqttMessage(char* topic, uint8_t* payload, unsigned int length);
bool subscribeRoomTopics();
void publishRoomCollaborationSummary();
RoomAggregateMetrics buildRoomAggregateMetrics();
bool mqttEndpointUsesWebSockets(const String& mqttHost);
bool mqttEndpointUsesSecureWebSockets(const String& mqttHost);
String normalizeWebSocketPath(const String& path);
String stripUrlScheme(const String& value, const char* scheme);
bool isIpv4AddressLiteral(const String& value);
void serviceMqttTransport();
void configureMqttTransport(bool useWebSockets, const String& mqttHost, uint16_t mqttPort, const String& webSocketPath, const String& hostHeader);
String homeAssistantDeviceTopic();
String homeAssistantDiscoveryTopic(const char* component, const char* objectId);
String homeAssistantStateTopic(const char* objectId);
String homeAssistantObservationTopic();
void ensureWiFiConnected();
void ensureMqttConnected();
void publishHomeAssistantAvailability(bool online);
void publishHomeAssistantDiscovery();
void publishHomeAssistantPresence();
void publishHomeAssistantDistance();
void publishObservationFeed();
void publishHomeAssistantDiagnostics();
void publishHomeAssistantDerivedMetrics();
void publishHomeAssistantStates();
RadarDerivedMetrics buildRadarDerivedMetrics();
RadarDerivedMetrics buildRadarDerivedMetrics(const LatestEnergyFrameSnapshot* energyFrame,
                                            const LatestTextFrameSnapshot* textFrame,
                                            bool gpioPresence);
uint16_t effectiveMinGateEnergy();
uint16_t effectiveMinGateEnergy(const RuntimeHomeAssistantConfig& config);
uint8_t effectiveMinActivityScore();
uint8_t effectiveMinActivityScore(const RuntimeHomeAssistantConfig& config);
bool radarDetectionCandidate(const RadarDerivedMetrics& metrics);
bool radarDetectionCandidate(const RadarDerivedMetrics& metrics,
                             const LatestEnergyFrameSnapshot* energyFrame,
                             const RuntimeHomeAssistantConfig& config);
RadarDetectionDecision radarDetectionDecision(const RadarDerivedMetrics& metrics);
RadarDetectionDecision radarDetectionDecision(const RadarDerivedMetrics& metrics,
                                              const LatestEnergyFrameSnapshot* energyFrame,
                                              const RuntimeHomeAssistantConfig& config);
uint16_t effectivePresenceHoldMs();
uint32_t presenceDecayRemainingMs(uint32_t now);
void updateStatusRgbLed(uint32_t now);
uint32_t statusLedColorForState(uint32_t now);
String statusLedHex(uint32_t color);
String buildHomeAssistantSensorPayload(const String& name,
                                       const String& uniqueId,
                                       const String& stateTopic,
                                       const String& availabilityTopic,
                                       const String& deviceJson,
                                       const String& extraFields);
void updateDistanceCm(int distanceCm);
uint8_t activeUdpDiscoveryPeerCount();
IPAddress localBroadcastIp();
const char* describeWiFiAuthMode(wifi_auth_mode_t mode);
const char* describeWiFiDisconnectReason(uint8_t reason);
const char* describeMqttState(int state);
void handleWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);

void serviceMqttTransport() {
  if (runtimeConfig.mqttUseWebSockets || mqttEndpointUsesWebSockets(runtimeConfig.mqttHost)) {
    HomeAssistantWebSocketClient.loop();
  }
}

void configureMqttTransport(bool useWebSockets, const String& mqttHost, uint16_t mqttPort, const String& webSocketPath, const String& hostHeader) {
  if (useWebSockets) {
    HomeAssistantWebSocketClient.configure(mqttHost, mqttPort, normalizeWebSocketPath(webSocketPath), hostHeader);
    HomeAssistantMqttClient.setClient(HomeAssistantWebSocketClient);
    HomeAssistantMqttClient.setServer(mqttHost.c_str(), mqttPort);
    return;
  }

  HomeAssistantMqttClient.setClient(HomeAssistantWiFiClient);
}

String deviceHostname() {
  String candidate = runtimeConfig.nodeId.length() > 0 ? runtimeConfig.nodeId : String(HomeAssistantConfig::kNodeId);
  return sanitizeHostname(candidate);
}

String deviceHostnameLabel() {
  return deviceHostname() + ".local";
}

String deviceDashboardUrl() {
  const String hostnameLabel = deviceHostnameLabel();
  if (hostnameLabel.length() > 0) {
    return "http://" + hostnameLabel + "/";
  }

  if (lastKnownIpAddress.length() > 0 && lastKnownIpAddress != "0.0.0.0") {
    return "http://" + lastKnownIpAddress + "/";
  }

  return String("http://0.0.0.0/");
}

String accessPointSsid() {
  String ssid = String("LB-MMWave-") + deviceHostname();
  if (ssid.length() > 31) {
    ssid.remove(31);
  }
  return ssid;
}

String accessPointPassword() {
  uint64_t chipMac = ESP.getEfuseMac();
  char password[16];
  snprintf(password, sizeof(password), "lbmmw%06llx", chipMac & 0xFFFFFFULL);
  return String(password);
}

void ensureAccessPointActive() {
  String hostname = deviceHostname();
  String ssid = accessPointSsid();
  String password = accessPointPassword();

  wifi_mode_t mode = WiFi.getMode();
  bool apModeActive = mode == WIFI_AP || mode == WIFI_AP_STA;

  if (apModeActive && lastSoftApSsid == ssid && lastSoftApPassword == password && lastSoftApIpAddress != "0.0.0.0") {
    return;
  }

  WiFi.mode(homeAssistantConfigured() ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAPdisconnect(true);
  WiFi.softAPsetHostname(hostname.c_str());
  WiFi.softAP(ssid.c_str(), password.c_str());

  lastDeviceHostname = hostname;
  lastSoftApSsid = ssid;
  lastSoftApPassword = password;
  lastSoftApIpAddress = WiFi.softAPIP().toString();

  DeviceDnsServer.stop();
  dnsServerStarted = DeviceDnsServer.start(AP_DNS_PORT, "*", WiFi.softAPIP());
}

void ensureMdnsActive() {
  String hostname = deviceHostname();

  if (WiFi.status() == WL_CONNECTED) {
    if (!mdnsStarted || lastDeviceHostname != hostname) {
      if (mdnsStarted) {
        MDNS.end();
      }

      mdnsStarted = MDNS.begin(hostname.c_str());
      if (mdnsStarted) {
        MDNS.addService("http", "tcp", DEVICE_HTTP_PORT);
      }

      lastDeviceHostname = hostname;
    }

    return;
  }

  if (mdnsStarted) {
    MDNS.end();
    mdnsStarted = false;
  }
}

String buildDeviceSnapshotJson(int32_t energySince,
                               int32_t textSince,
                               int32_t genericSince) {
  String json;
  json.reserve(6144);
  RadarDerivedMetrics metrics = buildRadarDerivedMetrics();
  uint32_t now = millis();
  uint32_t ledColor = statusLedColorForState(now);
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  const IPAddress broadcastIp = localBroadcastIp();

  json += "{";
  json += "\"enabled\":";
  json += runtimeConfig.enabled ? "true" : "false";
  json += ",\"configured\":";
  json += homeAssistantConfigured() ? "true" : "false";
  json += ",\"wifi_ssid\":\"" + jsonEscape(runtimeConfig.wifiSsid) + "\"";
  json += ",\"mqtt_host\":\"" + jsonEscape(runtimeConfig.mqttHost) + "\"";
  json += ",\"mqtt_port\":" + String(runtimeConfig.mqttPort);
  json += ",\"mqtt_transport\":\"" + String((runtimeConfig.mqttUseWebSockets || mqttEndpointUsesWebSockets(runtimeConfig.mqttHost)) ? "websocket" : "tcp") + "\"";
  json += ",\"mqtt_ws_path\":\"" + jsonEscape(normalizeWebSocketPath(runtimeConfig.mqttWebSocketPath)) + "\"";
  json += ",\"mqtt_host_header\":\"" + jsonEscape(runtimeConfig.mqttHostHeader) + "\"";
  json += ",\"mqtt_username_set\":";
  json += runtimeConfig.mqttUsername.length() > 0 ? "true" : "false";
  json += ",\"firmware_version\":\"" + jsonEscape(firmwareVersion()) + "\"";
  json += ",\"build_target\":\"" + jsonEscape(firmwareBuildTarget()) + "\"";
  json += ",\"git_sha\":\"" + jsonEscape(firmwareGitSha()) + "\"";
  json += ",\"node_id\":\"" + jsonEscape(runtimeConfig.nodeId) + "\"";
  json += ",\"friendly_name\":\"" + jsonEscape(runtimeConfig.friendlyName) + "\"";
  json += ",\"room_id\":\"" + jsonEscape(runtimeConfig.roomId) + "\"";
  json += ",\"sensor_role\":\"" + jsonEscape(runtimeConfig.sensorRole) + "\"";
  json += ",\"pose_x_cm\":" + String(runtimeConfig.roomPoseXCm);
  json += ",\"pose_y_cm\":" + String(runtimeConfig.roomPoseYCm);
  json += ",\"heading_deg\":" + String(runtimeConfig.roomHeadingDeg);
  json += ",\"room_width_cm\":" + String(runtimeConfig.roomWidthCm);
  json += ",\"room_height_cm\":" + String(runtimeConfig.roomHeightCm);
  json += ",\"max_detection_range_cm\":" + String(runtimeConfig.maxDetectionRangeCm);
  json += ",\"min_gate_energy\":" + String(runtimeConfig.minGateEnergy);
  json += ",\"sensitivity_percent\":" + String(runtimeConfig.sensitivityPercent);
  json += ",\"presence_hold_ms\":" + String(runtimeConfig.presenceHoldMs);
  json += ",\"min_active_gates\":" + String(runtimeConfig.minActiveGates);
  json += ",\"min_activity_score\":" + String(runtimeConfig.minActivityScore);
  json += ",\"led_enabled\":";
  json += runtimeConfig.ledEnabled ? "true" : "false";
  json += ",\"led_brightness\":" + String(runtimeConfig.ledBrightness);
  json += ",\"wifi_connected\":";
  json += wifiConnected ? "true" : "false";
  json += ",\"wifi_disconnect_reason\":" + String(lastWiFiDisconnectReason);
  json += ",\"wifi_disconnect_reason_text\":\"" + jsonEscape(lastWiFiDisconnectReasonText) + "\"";
  json += ",\"ip_address\":\"" + jsonEscape(lastKnownIpAddress) + "\"";
  json += ",\"wifi_link\":{";
  json += "\"connected\":";
  json += wifiConnected ? "true" : "false";
  json += ",\"ssid\":\"" + jsonEscape(wifiConnected ? WiFi.SSID() : runtimeConfig.wifiSsid) + "\"";
  json += ",\"rssi_dbm\":" + String(wifiConnected ? WiFi.RSSI() : 0);
  json += ",\"channel\":" + String(wifiConnected ? WiFi.channel() : 0);
  json += ",\"bssid\":\"" + jsonEscape(wifiConnected ? WiFi.BSSIDstr() : String("")) + "\"";
  json += ",\"mac_address\":\"" + jsonEscape(WiFi.macAddress()) + "\"";
  json += ",\"subnet_mask\":\"" + jsonEscape(wifiConnected ? WiFi.subnetMask().toString() : String("0.0.0.0")) + "\"";
  json += ",\"gateway_ip\":\"" + jsonEscape(wifiConnected ? WiFi.gatewayIP().toString() : String("0.0.0.0")) + "\"";
  json += ",\"dns_1\":\"" + jsonEscape(wifiConnected ? WiFi.dnsIP(0).toString() : String("0.0.0.0")) + "\"";
  json += ",\"dns_2\":\"" + jsonEscape(wifiConnected ? WiFi.dnsIP(1).toString() : String("0.0.0.0")) + "\"";
  json += ",\"broadcast_ip\":\"" + jsonEscape(wifiConnected ? broadcastIp.toString() : String("0.0.0.0")) + "\"";
  json += "}";
  json += ",\"mqtt_connected\":";
  json += HomeAssistantMqttClient.connected() ? "true" : "false";
  json += ",\"mqtt_state\":" + String(lastMqttState);
  json += ",\"mqtt_state_text\":\"" + jsonEscape(lastMqttStateText) + "\"";
  json += ",\"mqtt_host_ip\":\"" + jsonEscape(lastResolvedMqttHost) + "\"";
  json += ",\"topic_prefix\":\"" + jsonEscape(homeAssistantDeviceTopic()) + "\"";
  json += ",\"device_hostname\":\"" + jsonEscape(deviceHostname()) + "\"";
  json += ",\"dashboard_url\":\"" + jsonEscape(deviceDashboardUrl()) + "\"";
  json += ",\"ap_ssid\":\"" + jsonEscape(lastSoftApSsid) + "\"";
  json += ",\"ap_password\":\"" + jsonEscape(lastSoftApPassword) + "\"";
  json += ",\"ap_ip\":\"" + jsonEscape(lastSoftApIpAddress) + "\"";
  json += ",\"uptime_ms\":" + String(millis() - bootMillis);
  json += ",\"free_heap\":" + String(ESP.getFreeHeap());
  json += ",\"presence\":";
  json += lastPresence ? "true" : "false";
  json += ",\"gpio_presence\":";
  json += lastGpioPresence ? "true" : "false";
  json += ",\"detection_candidate\":";
  json += detectionCandidateActive ? "true" : "false";
  json += ",\"presence_decay_remaining_ms\":" + String(presenceDecayRemainingMs(now));
  json += ",\"radar_bytes_total\":" + String(radarBytesTotal);
  json += ",\"radar_frames_total\":" + String(radarFramesTotal);
  json += ",\"ld2420_energy_frames_total\":" + String(ld2420EnergyFramesTotal);
  json += ",\"presence_changes_total\":" + String(presenceChangesTotal);
  json += ",\"people_estimate\":" + String(metrics.estimatedPeople);
  json += ",\"active_gate_count\":" + String(metrics.activeGateCount);
  json += ",\"activity_score\":" + String(metrics.activityScore);
  json += ",\"dominant_gate_index\":" + String(metrics.dominantGateIndex);
  json += ",\"dominant_gate_distance_cm\":" + String(metrics.dominantGateDistanceCm);
  json += ",\"dominant_gate_energy\":" + String(metrics.dominantGateEnergy);
  json += ",\"total_gate_energy\":" + String(metrics.totalGateEnergy);
  json += ",\"status_led_hex\":\"" + statusLedHex(ledColor) + "\"";
  json += ",\"runtime_benchmark\":";
  if (latestRuntimeBenchmarkSnapshot.valid) {
    json += "{";
    json += "\"measured_at_ms\":" + String(latestRuntimeBenchmarkSnapshot.measuredAtMs);
    json += ",\"iterations\":" + String(latestRuntimeBenchmarkSnapshot.iterations);
    json += ",\"parse_command_fixture\":{";
    json += "\"total_us\":" + String(latestRuntimeBenchmarkSnapshot.parseCommandFixture.totalUs);
    json += ",\"per_iter_ns\":" + String(latestRuntimeBenchmarkSnapshot.parseCommandFixture.perIterNs);
    json += "}";
    json += ",\"parse_room_config_fixture\":{";
    json += "\"total_us\":" + String(latestRuntimeBenchmarkSnapshot.parseRoomConfigFixture.totalUs);
    json += ",\"per_iter_ns\":" + String(latestRuntimeBenchmarkSnapshot.parseRoomConfigFixture.perIterNs);
    json += "}";
    json += ",\"parse_tuning_config_fixture\":{";
    json += "\"total_us\":" + String(latestRuntimeBenchmarkSnapshot.parseTuningConfigFixture.totalUs);
    json += ",\"per_iter_ns\":" + String(latestRuntimeBenchmarkSnapshot.parseTuningConfigFixture.perIterNs);
    json += "}";
    json += ",\"parse_generic_fixture\":{";
    json += "\"total_us\":" + String(latestRuntimeBenchmarkSnapshot.parseGenericFixture.totalUs);
    json += ",\"per_iter_ns\":" + String(latestRuntimeBenchmarkSnapshot.parseGenericFixture.perIterNs);
    json += "}";
    json += ",\"derive_metrics_fixture\":{";
    json += "\"total_us\":" + String(latestRuntimeBenchmarkSnapshot.deriveMetricsFixture.totalUs);
    json += ",\"per_iter_ns\":" + String(latestRuntimeBenchmarkSnapshot.deriveMetricsFixture.perIterNs);
    json += "}";
    json += ",\"detection_candidate_fixture\":{";
    json += "\"total_us\":" + String(latestRuntimeBenchmarkSnapshot.detectionCandidateFixture.totalUs);
    json += ",\"per_iter_ns\":" + String(latestRuntimeBenchmarkSnapshot.detectionCandidateFixture.perIterNs);
    json += "}";
    json += ",\"detection_candidate\":" + String(latestRuntimeBenchmarkSnapshot.detectionCandidate ? "true" : "false");
    json += ",\"people_estimate\":" + String(latestRuntimeBenchmarkSnapshot.peopleEstimate);
    json += ",\"active_gate_count\":" + String(latestRuntimeBenchmarkSnapshot.activeGateCount);
    json += ",\"activity_score\":" + String(latestRuntimeBenchmarkSnapshot.activityScore);
    json += ",\"dominant_gate_distance_cm\":" + String(latestRuntimeBenchmarkSnapshot.dominantGateDistanceCm);
    json += "}";
  } else {
    json += "null";
  }

  RoomAggregateMetrics roomMetrics = buildRoomAggregateMetrics();
  json += ",\"room_people_estimate\":" + String(roomMetrics.peopleEstimate);
  json += ",\"room_active_nodes\":" + String(roomMetrics.activeNodeCount);
  json += ",\"room_peer_nodes\":" + String(roomMetrics.peerNodeCount);
  json += ",\"room_activity_score\":" + String(roomMetrics.activityScore);
  appendFirmwareSyncJson(json);
  appendUdpDiscoveryPeersJson(json);
  appendRoomPeersJson(json, metrics.dominantGateDistanceCm);
  appendBleSightingsJson(json);
  appendBleTagsJson(json);
  json += ",\"latest_energy_frame\":";
  if (latestEnergyFrameSnapshot.valid && static_cast<int32_t>(latestEnergyFrameSnapshot.framesTotal) != energySince) {
    json += "{";
    json += "\"length\":" + String(latestEnergyFrameSnapshot.length);
    json += ",\"payload_length\":" + String(latestEnergyFrameSnapshot.payloadLength);
    json += ",\"presence\":";
    json += latestEnergyFrameSnapshot.presence ? "true" : "false";
    json += ",\"distance_cm\":" + String(latestEnergyFrameSnapshot.distanceCm);
    json += ",\"bytes_total\":" + String(latestEnergyFrameSnapshot.bytesTotal);
    json += ",\"frames_total\":" + String(latestEnergyFrameSnapshot.framesTotal);
    json += ",\"energy_frames_total\":" + String(latestEnergyFrameSnapshot.energyFramesTotal);
    json += ",\"gates\":[";
    for (size_t i = 0; i < 16; i++) {
      if (i > 0) {
        json += ",";
      }
      json += String(latestEnergyFrameSnapshot.gates[i]);
    }
    json += "]}";
  } else {
    json += "null";
  }

  json += ",\"latest_text_frame\":";
  if (latestTextFrameSnapshot.valid && static_cast<int32_t>(latestTextFrameSnapshot.framesTotal) != textSince) {
    json += "{";
    json += "\"length\":" + String(latestTextFrameSnapshot.length);
    json += ",\"presence\":";
    json += latestTextFrameSnapshot.presence ? "true" : "false";
    json += ",\"range\":" + String(latestTextFrameSnapshot.range);
    json += ",\"bytes_total\":" + String(latestTextFrameSnapshot.bytesTotal);
    json += ",\"frames_total\":" + String(latestTextFrameSnapshot.framesTotal);
    json += ",\"hex\":\"" + jsonEscape(latestTextFrameSnapshot.hex) + "\"";
    json += ",\"ascii\":\"" + jsonEscape(latestTextFrameSnapshot.ascii) + "\"";
    json += "}";
  } else {
    json += "null";
  }

  json += ",\"latest_generic_frame\":";
  if (latestGenericFrameSnapshot.valid && static_cast<int32_t>(latestGenericFrameSnapshot.framesTotal) != genericSince) {
    json += "{";
    json += "\"length\":" + String(latestGenericFrameSnapshot.length);
    json += ",\"bytes_total\":" + String(latestGenericFrameSnapshot.bytesTotal);
    json += ",\"frames_total\":" + String(latestGenericFrameSnapshot.framesTotal);
    json += ",\"hex\":\"" + jsonEscape(latestGenericFrameSnapshot.hex) + "\"";
    json += ",\"ascii\":\"" + jsonEscape(latestGenericFrameSnapshot.ascii) + "\"";
    json += "}";
  } else {
    json += "null";
  }

  json += "}";
  return json;
}

const char* radarDetectionDecisionReason(RadarDetectionDecision decision) {
  switch (decision) {
    case RadarDetectionDecision::Candidate: return "radar_candidate";
    case RadarDetectionDecision::InvalidMetrics: return "invalid_metrics";
    case RadarDetectionDecision::OutOfRange: return "out_of_range";
    case RadarDetectionDecision::InsufficientActiveGates: return "insufficient_active_gates";
    case RadarDetectionDecision::LowActivity: return "low_activity";
    case RadarDetectionDecision::MissingEnergyFrame: return "missing_energy_frame";
    case RadarDetectionDecision::LowEnergy: return "low_energy";
    case RadarDetectionDecision::NearFieldClutter: return "near_field_clutter";
    default: return "unknown";
  }
}

const char* statusLedPhase(uint32_t now) {
  if (!runtimeConfig.ledEnabled) {
    return "disabled";
  }

  if (detectionCandidateActive) {
    return "active_detection";
  }

  if (presenceDecayRemainingMs(now) > 0) {
    return "presence_decay";
  }

  return "idle";
}

String buildDebugStatusJson() {
  RadarDerivedMetrics metrics = buildRadarDerivedMetrics();
  const LatestEnergyFrameSnapshot* energyFrame = latestEnergyFrameSnapshot.valid ? &latestEnergyFrameSnapshot : nullptr;
  const RadarDetectionDecision decision = radarDetectionDecision(metrics, energyFrame, runtimeConfig);
  const bool gpioFallback = lastGpioPresence && !latestEnergyFrameSnapshot.valid && !latestTextFrameSnapshot.valid;
  const bool radarCandidate = decision == RadarDetectionDecision::Candidate;
  const uint32_t now = millis();

  String json;
  json.reserve(512);
  json += "{";
  json += "\"status_led_hex\":\"" + statusLedHex(statusLedColorForState(now)) + "\"";
  json += ",\"led_phase\":\"" + String(statusLedPhase(now)) + "\"";
  json += ",\"detection_reason\":\"";
  json += gpioFallback && !radarCandidate ? "gpio_fallback" : radarDetectionDecisionReason(decision);
  json += "\"";
  json += ",\"detection_candidate\":";
  json += detectionCandidateActive ? "true" : "false";
  json += ",\"presence\":";
  json += lastPresence ? "true" : "false";
  json += ",\"gpio_presence\":";
  json += lastGpioPresence ? "true" : "false";
  json += ",\"radar_candidate\":";
  json += radarCandidate ? "true" : "false";
  json += ",\"gpio_fallback\":";
  json += gpioFallback ? "true" : "false";
  json += ",\"clutter_suppressed\":";
  json += decision == RadarDetectionDecision::NearFieldClutter ? "true" : "false";
  json += ",\"presence_decay_remaining_ms\":" + String(presenceDecayRemainingMs(now));
  json += ",\"effective_min_gate_energy\":" + String(effectiveMinGateEnergy(runtimeConfig));
  json += ",\"effective_min_activity_score\":" + String(effectiveMinActivityScore(runtimeConfig));
  json += ",\"active_gate_count\":" + String(metrics.activeGateCount);
  json += ",\"activity_score\":" + String(metrics.activityScore);
  json += ",\"dominant_gate_distance_cm\":" + String(metrics.dominantGateDistanceCm);
  json += ",\"dominant_gate_energy\":" + String(metrics.dominantGateEnergy);
  json += "}";
  return json;
}

const char* describeWiFiDisconnectReason(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_UNSPECIFIED: return "UNSPECIFIED";
    case WIFI_REASON_AUTH_EXPIRE: return "AUTH_EXPIRE";
    case WIFI_REASON_AUTH_LEAVE: return "AUTH_LEAVE";
    case WIFI_REASON_ASSOC_EXPIRE: return "ASSOC_EXPIRE";
    case WIFI_REASON_ASSOC_TOOMANY: return "ASSOC_TOOMANY";
    case WIFI_REASON_NOT_AUTHED: return "NOT_AUTHED";
    case WIFI_REASON_NOT_ASSOCED: return "NOT_ASSOCED";
    case WIFI_REASON_ASSOC_LEAVE: return "ASSOC_LEAVE";
    case WIFI_REASON_ASSOC_NOT_AUTHED: return "ASSOC_NOT_AUTHED";
    case WIFI_REASON_DISASSOC_PWRCAP_BAD: return "DISASSOC_PWRCAP_BAD";
    case WIFI_REASON_DISASSOC_SUPCHAN_BAD: return "DISASSOC_SUPCHAN_BAD";
    case WIFI_REASON_IE_INVALID: return "IE_INVALID";
    case WIFI_REASON_MIC_FAILURE: return "MIC_FAILURE";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4WAY_HANDSHAKE_TIMEOUT";
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "GROUP_KEY_UPDATE_TIMEOUT";
    case WIFI_REASON_IE_IN_4WAY_DIFFERS: return "IE_IN_4WAY_DIFFERS";
    case WIFI_REASON_GROUP_CIPHER_INVALID: return "GROUP_CIPHER_INVALID";
    case WIFI_REASON_PAIRWISE_CIPHER_INVALID: return "PAIRWISE_CIPHER_INVALID";
    case WIFI_REASON_AKMP_INVALID: return "AKMP_INVALID";
    case WIFI_REASON_UNSUPP_RSN_IE_VERSION: return "UNSUPP_RSN_IE_VERSION";
    case WIFI_REASON_INVALID_RSN_IE_CAP: return "INVALID_RSN_IE_CAP";
    case WIFI_REASON_802_1X_AUTH_FAILED: return "802_1X_AUTH_FAILED";
    case WIFI_REASON_CIPHER_SUITE_REJECTED: return "CIPHER_SUITE_REJECTED";
    case WIFI_REASON_BEACON_TIMEOUT: return "BEACON_TIMEOUT";
    case WIFI_REASON_NO_AP_FOUND: return "NO_AP_FOUND";
    case WIFI_REASON_AUTH_FAIL: return "AUTH_FAIL";
    case WIFI_REASON_ASSOC_FAIL: return "ASSOC_FAIL";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "HANDSHAKE_TIMEOUT";
    default: return "UNKNOWN";
  }
}

const char* describeMqttState(int state) {
  switch (state) {
    case -4: return "CONNECTION_TIMEOUT";
    case -3: return "CONNECTION_LOST";
    case -2: return "CONNECT_FAILED";
    case -1: return "DISCONNECTED";
    case 0: return "CONNECTED";
    case 1: return "BAD_PROTOCOL";
    case 2: return "BAD_CLIENT_ID";
    case 3: return "UNAVAILABLE";
    case 4: return "BAD_CREDENTIALS";
    case 5: return "NOT_AUTHORIZED";
    default: return "UNKNOWN";
  }
}

const char* describeWiFiAuthMode(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA_PSK";
    case WIFI_AUTH_WPA2_PSK: return "WPA2_PSK";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA_WPA2_PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2_ENTERPRISE";
    case WIFI_AUTH_WPA3_PSK: return "WPA3_PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2_WPA3_PSK";
    case WIFI_AUTH_WAPI_PSK: return "WAPI_PSK";
    default: return "UNKNOWN";
  }
}

void handleWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    lastKnownIpAddress = WiFi.localIP().toString();
    lastWiFiDisconnectReason = 0;
    lastWiFiDisconnectReasonText = "connected";
    lastUdpDiscoveryAnnounceMs = 0;
    ensureMdnsActive();
    return;
  }

  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    lastKnownIpAddress = "0.0.0.0";
    lastWiFiDisconnectReason = info.wifi_sta_disconnected.reason;
    lastWiFiDisconnectReasonText = describeWiFiDisconnectReason(info.wifi_sta_disconnected.reason);
    if (udpDiscoveryStarted) {
      DeviceUdpDiscovery.stop();
      udpDiscoveryStarted = false;
    }
    ensureMdnsActive();
  }
}

void loadHomeAssistantConfig() {
  runtimeConfig.enabled = SettingsStore.getBool("enabled", HomeAssistantConfig::kEnabled);
  runtimeConfig.wifiSsid = SettingsStore.getString("wifi_ssid", HomeAssistantConfig::kWifiSsid);
  runtimeConfig.wifiPassword = SettingsStore.getString("wifi_pass", HomeAssistantConfig::kWifiPassword);
  runtimeConfig.mqttHost = SettingsStore.getString("mqtt_host", HomeAssistantConfig::kMqttHost);
  runtimeConfig.mqttPort = SettingsStore.getUShort("mqtt_port", HomeAssistantConfig::kMqttPort);
  runtimeConfig.mqttUsername = SettingsStore.getString("mqtt_user", HomeAssistantConfig::kMqttUsername);
  runtimeConfig.mqttPassword = SettingsStore.getString("mqtt_pass", HomeAssistantConfig::kMqttPassword);
  runtimeConfig.mqttUseWebSockets = SettingsStore.getBool("mqtt_ws", HomeAssistantConfig::kMqttUseWebSockets);
  runtimeConfig.mqttWebSocketPath = SettingsStore.getString("mqtt_wsp", HomeAssistantConfig::kMqttWebSocketPath);
  runtimeConfig.mqttHostHeader = SettingsStore.getString("mqtt_hdr", HomeAssistantConfig::kMqttHostHeader);

  String storedNodeId = SettingsStore.getString("node_id", "");
  runtimeConfig.nodeId = usesFactoryDefaultIdentity(storedNodeId, HomeAssistantConfig::kNodeId)
    ? defaultNodeId()
    : storedNodeId;

  String storedFriendlyName = SettingsStore.getString("friendly", "");
  runtimeConfig.friendlyName = usesFactoryDefaultIdentity(storedFriendlyName, HomeAssistantConfig::kFriendlyName)
    ? defaultFriendlyName()
    : storedFriendlyName;
  runtimeConfig.roomId = SettingsStore.getString("room_id", defaultRoomId());
  runtimeConfig.sensorRole = SettingsStore.getString("sensor_role", defaultSensorRole());
  runtimeConfig.roomPoseXCm = static_cast<int16_t>(SettingsStore.getShort("pose_x", 0));
  runtimeConfig.roomPoseYCm = static_cast<int16_t>(SettingsStore.getShort("pose_y", 0));
  runtimeConfig.roomHeadingDeg = static_cast<int16_t>(SettingsStore.getShort("pose_h", -90));
  runtimeConfig.roomWidthCm = SettingsStore.getUShort("room_w", 600);
  runtimeConfig.roomHeightCm = SettingsStore.getUShort("room_h", 400);
  runtimeConfig.maxDetectionRangeCm = SettingsStore.getUShort("max_range", LD2420_GATE_COUNT * LD2420_GATE_SIZE_CM);
  runtimeConfig.minGateEnergy = SettingsStore.getUShort("min_energy", LD2420_ACTIVE_GATE_FLOOR);
  runtimeConfig.sensitivityPercent = SettingsStore.getUChar("sense_pct", 55);
  runtimeConfig.presenceHoldMs = static_cast<uint16_t>(constrain(SettingsStore.getUShort("hold_ms", DEFAULT_PRESENCE_HOLD_MS),
                                                                MIN_PRESENCE_HOLD_MS,
                                                                MAX_PRESENCE_HOLD_MS));
  runtimeConfig.minActiveGates = SettingsStore.getUChar("min_gates", 1);
  runtimeConfig.minActivityScore = SettingsStore.getUChar("min_act", 10);
  runtimeConfig.ledEnabled = SettingsStore.getBool("led_on", true);
  runtimeConfig.ledBrightness = SettingsStore.getUChar("led_bri", DEFAULT_LED_BRIGHTNESS);

  for (uint8_t tagIndex = 0; tagIndex < MAX_BLE_TAGS; tagIndex++) {
    char labelKey[16];
    char addressKey[16];
    char rssiKey[16];
    snprintf(labelKey, sizeof(labelKey), "btag%u_lbl", tagIndex);
    snprintf(addressKey, sizeof(addressKey), "btag%u_mac", tagIndex);
    snprintf(rssiKey, sizeof(rssiKey), "btag%u_rssi", tagIndex);

    bleIdentityTags[tagIndex].label = SettingsStore.getString(labelKey, "");
    bleIdentityTags[tagIndex].address = normalizeBleIdentityValue(SettingsStore.getString(addressKey, ""));
    bleIdentityTags[tagIndex].minRssi = SettingsStore.getInt(rssiKey, BLE_TAG_DEFAULT_MIN_RSSI);
    bleIdentityTags[tagIndex].lastRssi = -127;
    bleIdentityTags[tagIndex].lastSeenMs = 0;
    bleIdentityTags[tagIndex].occupied = bleIdentityTags[tagIndex].label.length() > 0 && bleIdentityTags[tagIndex].address.length() > 0;
  }
}

void saveHomeAssistantConfig() {
  SettingsStore.putBool("enabled", runtimeConfig.enabled);
  SettingsStore.putString("wifi_ssid", runtimeConfig.wifiSsid);
  SettingsStore.putString("wifi_pass", runtimeConfig.wifiPassword);
  SettingsStore.putString("mqtt_host", runtimeConfig.mqttHost);
  SettingsStore.putUShort("mqtt_port", runtimeConfig.mqttPort);
  SettingsStore.putString("mqtt_user", runtimeConfig.mqttUsername);
  SettingsStore.putString("mqtt_pass", runtimeConfig.mqttPassword);
  SettingsStore.putBool("mqtt_ws", runtimeConfig.mqttUseWebSockets);
  SettingsStore.putString("mqtt_wsp", normalizeWebSocketPath(runtimeConfig.mqttWebSocketPath));
  SettingsStore.putString("mqtt_hdr", runtimeConfig.mqttHostHeader);
  SettingsStore.putString("node_id", runtimeConfig.nodeId);
  SettingsStore.putString("friendly", runtimeConfig.friendlyName);
  SettingsStore.putString("room_id", runtimeConfig.roomId);
  SettingsStore.putString("sensor_role", runtimeConfig.sensorRole);
  SettingsStore.putShort("pose_x", runtimeConfig.roomPoseXCm);
  SettingsStore.putShort("pose_y", runtimeConfig.roomPoseYCm);
  SettingsStore.putShort("pose_h", runtimeConfig.roomHeadingDeg);
  SettingsStore.putUShort("room_w", runtimeConfig.roomWidthCm);
  SettingsStore.putUShort("room_h", runtimeConfig.roomHeightCm);
  SettingsStore.putUShort("max_range", runtimeConfig.maxDetectionRangeCm);
  SettingsStore.putUShort("min_energy", runtimeConfig.minGateEnergy);
  SettingsStore.putUChar("sense_pct", runtimeConfig.sensitivityPercent);
  SettingsStore.putUShort("hold_ms", runtimeConfig.presenceHoldMs);
  SettingsStore.putUChar("min_gates", runtimeConfig.minActiveGates);
  SettingsStore.putUChar("min_act", runtimeConfig.minActivityScore);
  SettingsStore.putBool("led_on", runtimeConfig.ledEnabled);
  SettingsStore.putUChar("led_bri", runtimeConfig.ledBrightness);

  for (uint8_t tagIndex = 0; tagIndex < MAX_BLE_TAGS; tagIndex++) {
    char labelKey[16];
    char addressKey[16];
    char rssiKey[16];
    snprintf(labelKey, sizeof(labelKey), "btag%u_lbl", tagIndex);
    snprintf(addressKey, sizeof(addressKey), "btag%u_mac", tagIndex);
    snprintf(rssiKey, sizeof(rssiKey), "btag%u_rssi", tagIndex);

    SettingsStore.putString(labelKey, bleIdentityTags[tagIndex].occupied ? bleIdentityTags[tagIndex].label : "");
    SettingsStore.putString(addressKey, bleIdentityTags[tagIndex].occupied ? bleIdentityTags[tagIndex].address : "");
    SettingsStore.putInt(rssiKey, bleIdentityTags[tagIndex].occupied ? bleIdentityTags[tagIndex].minRssi : BLE_TAG_DEFAULT_MIN_RSSI);
  }
}

void resetHomeAssistantConnections() {
  homeAssistantDiscoveryPublished = false;
  lastWiFiConnectAttemptMs = 0;
  lastMqttConnectAttemptMs = 0;

  if (HomeAssistantMqttClient.connected()) {
    HomeAssistantMqttClient.disconnect();
  }

  HomeAssistantWebSocketClient.stop();

  WiFi.disconnect(false, false);
}

void emitHomeAssistantConfigEvent() {
  printJsonEventPrefix("ha_config");

  Serial.print(",\"enabled\":");
  Serial.print(runtimeConfig.enabled ? "true" : "false");

  Serial.print(",\"configured\":");
  Serial.print(homeAssistantConfigured() ? "true" : "false");

  Serial.print(",\"wifi_ssid\":");
  printJsonString(runtimeConfig.wifiSsid);

  Serial.print(",\"mqtt_host\":");
  printJsonString(runtimeConfig.mqttHost);

  Serial.print(",\"mqtt_port\":");
  Serial.print(runtimeConfig.mqttPort);

  Serial.print(",\"mqtt_transport\":");
  printJsonString((runtimeConfig.mqttUseWebSockets || mqttEndpointUsesWebSockets(runtimeConfig.mqttHost)) ? "websocket" : "tcp");

  Serial.print(",\"mqtt_ws_path\":");
  printJsonString(normalizeWebSocketPath(runtimeConfig.mqttWebSocketPath));

  Serial.print(",\"mqtt_host_header\":");
  printJsonString(runtimeConfig.mqttHostHeader);

  Serial.print(",\"mqtt_username_set\":");
  Serial.print(runtimeConfig.mqttUsername.length() > 0 ? "true" : "false");

  Serial.print(",\"node_id\":");
  printJsonString(runtimeConfig.nodeId);

  Serial.print(",\"friendly_name\":");
  printJsonString(runtimeConfig.friendlyName);

  Serial.print(",\"room_id\":");
  printJsonString(runtimeConfig.roomId);

  Serial.print(",\"sensor_role\":");
  printJsonString(runtimeConfig.sensorRole);

  Serial.print(",\"pose_x_cm\":");
  Serial.print(runtimeConfig.roomPoseXCm);

  Serial.print(",\"pose_y_cm\":");
  Serial.print(runtimeConfig.roomPoseYCm);

  Serial.print(",\"heading_deg\":");
  Serial.print(runtimeConfig.roomHeadingDeg);

  Serial.print(",\"room_width_cm\":");
  Serial.print(runtimeConfig.roomWidthCm);

  Serial.print(",\"room_height_cm\":");
  Serial.print(runtimeConfig.roomHeightCm);

  Serial.print(",\"max_detection_range_cm\":");
  Serial.print(runtimeConfig.maxDetectionRangeCm);

  Serial.print(",\"min_gate_energy\":");
  Serial.print(runtimeConfig.minGateEnergy);

  Serial.print(",\"sensitivity_percent\":");
  Serial.print(runtimeConfig.sensitivityPercent);

  Serial.print(",\"presence_hold_ms\":");
  Serial.print(runtimeConfig.presenceHoldMs);

  Serial.print(",\"min_active_gates\":");
  Serial.print(runtimeConfig.minActiveGates);

  Serial.print(",\"min_activity_score\":");
  Serial.print(runtimeConfig.minActivityScore);

  Serial.print(",\"led_enabled\":");
  Serial.print(runtimeConfig.ledEnabled ? "true" : "false");

  Serial.print(",\"led_brightness\":");
  Serial.print(runtimeConfig.ledBrightness);

  Serial.print(",\"wifi_connected\":");
  Serial.print(WiFi.status() == WL_CONNECTED ? "true" : "false");

  Serial.print(",\"wifi_disconnect_reason\":");
  Serial.print(lastWiFiDisconnectReason);

  Serial.print(",\"wifi_disconnect_reason_text\":");
  printJsonString(lastWiFiDisconnectReasonText);

  Serial.print(",\"ip_address\":");
  printJsonString(lastKnownIpAddress);

  Serial.print(",\"mqtt_connected\":");
  Serial.print(HomeAssistantMqttClient.connected() ? "true" : "false");

  Serial.print(",\"mqtt_state\":");
  Serial.print(lastMqttState);

  Serial.print(",\"mqtt_state_text\":");
  printJsonString(lastMqttStateText);

  Serial.print(",\"mqtt_host_ip\":");
  printJsonString(lastResolvedMqttHost);

  Serial.print(",\"device_hostname\":");
  printJsonString(deviceHostname());

  Serial.print(",\"ap_ssid\":");
  printJsonString(lastSoftApSsid);

  Serial.print(",\"ap_password\":");
  printJsonString(lastSoftApPassword);

  Serial.print(",\"ap_ip\":");
  printJsonString(lastSoftApIpAddress);

  Serial.print(",\"topic_prefix\":");
  printJsonString(homeAssistantDeviceTopic());

  finishJsonEvent();
}

void emitWiFiScanResults() {
  WiFi.mode(WIFI_AP_STA);
  int count = WiFi.scanNetworks(false, true, false, 300, 0);
  lastWiFiScanResultCount = 0;
  lastWiFiScanMs = millis();
  wifiScansTotal++;

  printJsonEventPrefix("wifi_scan_results");
  Serial.print(",\"count\":");
  Serial.print(count > 0 ? count : 0);
  Serial.print(",\"networks\":[");

  if (count > 0) {
    for (int i = 0; i < count; i++) {
      if (lastWiFiScanResultCount < MAX_WIFI_SCAN_RESULTS) {
        lastWiFiScanResults[lastWiFiScanResultCount].ssid = WiFi.SSID(i);
        lastWiFiScanResults[lastWiFiScanResultCount].bssid = WiFi.BSSIDstr(i);
        lastWiFiScanResults[lastWiFiScanResultCount].authMode = describeWiFiAuthMode(static_cast<wifi_auth_mode_t>(WiFi.encryptionType(i)));
        lastWiFiScanResults[lastWiFiScanResultCount].rssi = WiFi.RSSI(i);
        lastWiFiScanResults[lastWiFiScanResultCount].channel = WiFi.channel(i);
        lastWiFiScanResults[lastWiFiScanResultCount].open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
        lastWiFiScanResultCount++;
      }

      if (i > 0) {
        Serial.print(",");
      }

      Serial.print("{\"ssid\":");
      printJsonString(WiFi.SSID(i));
  Serial.print(",\"bssid\":");
  printJsonString(WiFi.BSSIDstr(i));
  Serial.print(",\"auth_mode\":");
  printJsonString(describeWiFiAuthMode(static_cast<wifi_auth_mode_t>(WiFi.encryptionType(i))));
      Serial.print(",\"rssi\":");
      Serial.print(WiFi.RSSI(i));
      Serial.print(",\"channel\":");
      Serial.print(WiFi.channel(i));
      Serial.print(",\"open\":");
      Serial.print(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false");
      Serial.print("}");
    }
  }

  Serial.print("]");
  finishJsonEvent();
  WiFi.scanDelete();
}

bool homeAssistantConfigured() {
  return runtimeConfig.enabled &&
         runtimeConfig.wifiSsid.length() > 0 &&
         runtimeConfig.mqttHost.length() > 0 &&
         runtimeConfig.nodeId.length() > 0;
}

String homeAssistantDeviceTopic() {
  return String(HomeAssistantConfig::kTopicPrefix) + "/" + runtimeConfig.nodeId;
}

String homeAssistantDiscoveryTopic(const char* component, const char* objectId) {
  return String(HomeAssistantConfig::kDiscoveryPrefix) + "/" + component + "/" + runtimeConfig.nodeId + "/" + objectId + "/config";
}

String homeAssistantStateTopic(const char* objectId) {
  return homeAssistantDeviceTopic() + "/" + objectId + "/state";
}

String homeAssistantObservationTopic() {
  return homeAssistantDeviceTopic() + "/observations";
}

String roomTopicRoot() {
  return String(HomeAssistantConfig::kTopicPrefix) + "/rooms/" + sanitizeHostname(runtimeConfig.roomId);
}

String roomNodeSummaryTopic(const String& nodeId) {
  return roomTopicRoot() + "/nodes/" + nodeId + "/summary";
}

String roomSummarySubscriptionTopic() {
  return roomTopicRoot() + "/nodes/+/summary";
}

String roomPoseCommandTopic(const String& nodeId) {
  return roomTopicRoot() + "/nodes/" + nodeId + "/pose/set";
}

String roomPoseCommandSubscriptionTopic() {
  return roomTopicRoot() + "/nodes/+/pose/set";
}

IPAddress localBroadcastIp() {
  if (WiFi.status() != WL_CONNECTED) {
    return IPAddress(255, 255, 255, 255);
  }

  IPAddress localIp = WiFi.localIP();
  IPAddress subnetMask = WiFi.subnetMask();
  IPAddress broadcast;
  for (uint8_t index = 0; index < 4; index++) {
    broadcast[index] = localIp[index] | static_cast<uint8_t>(~subnetMask[index]);
  }
  return broadcast;
}

uint8_t activeUdpDiscoveryPeerCount() {
  const uint32_t now = millis();
  uint8_t count = 0;
  for (uint8_t index = 0; index < MAX_UDP_DISCOVERY_PEERS; index++) {
    if (udpDiscoveryPeers[index].occupied && (now - udpDiscoveryPeers[index].lastSeenMs) <= UDP_DISCOVERY_PEER_FRESHNESS_MS) {
      count++;
    }
  }
  return count;
}

void appendUdpDiscoveryPeersJson(String& json) {
  const uint32_t now = millis();
  json += ",\"udp_discovery\":{";
  json += "\"started\":";
  json += udpDiscoveryStarted ? "true" : "false";
  json += ",\"port\":" + String(UDP_DISCOVERY_PORT);
  json += ",\"peer_count\":" + String(activeUdpDiscoveryPeerCount());
  json += ",\"last_announce_ms\":" + String(lastUdpDiscoveryAnnounceMs);
  json += ",\"peers\":[";

  bool first = true;
  for (uint8_t index = 0; index < MAX_UDP_DISCOVERY_PEERS; index++) {
    const UdpDiscoveryPeer& peer = udpDiscoveryPeers[index];
    if (!peer.occupied || (now - peer.lastSeenMs) > UDP_DISCOVERY_PEER_FRESHNESS_MS) {
      continue;
    }

    if (!first) {
      json += ",";
    }
    first = false;

    json += "{\"node_id\":\"" + jsonEscape(peer.nodeId) + "\"";
    json += ",\"friendly_name\":\"" + jsonEscape(peer.friendlyName) + "\"";
    json += ",\"room_id\":\"" + jsonEscape(peer.roomId) + "\"";
    json += ",\"sensor_role\":\"" + jsonEscape(peer.sensorRole) + "\"";
    json += ",\"firmware_version\":\"" + jsonEscape(peer.firmwareVersion) + "\"";
    json += ",\"build_target\":\"" + jsonEscape(peer.buildTarget) + "\"";
    json += ",\"hostname\":\"" + jsonEscape(peer.hostname) + "\"";
    json += ",\"ip_address\":\"" + jsonEscape(peer.ipAddress) + "\"";
    json += ",\"wifi_rssi_dbm\":" + String(peer.wifiRssi);
    json += ",\"wifi_channel\":" + String(peer.wifiChannel);
    json += ",\"uptime_s\":" + String(peer.uptimeSeconds);
    json += ",\"free_heap_bytes\":" + String(peer.freeHeapBytes);
    json += ",\"age_ms\":" + String(now - peer.lastSeenMs);
    json += "}";
  }

  json += "]}";
}

void updateRoomPeerAngleHint(RoomPeerSummary& peer, int localDominantGateDistanceCm, int peerDominantGateDistanceCm) {
  if (localDominantGateDistanceCm < 0 || peerDominantGateDistanceCm < 0) {
    peer.relativeAngleGuessDeg = 0;
    peer.relativeAngleConfidencePercent = 0;
    peer.lastObservedDistanceCm = peerDominantGateDistanceCm;
    return;
  }

  if (lastLocalDominantGateDistanceCm < 0 || peer.lastObservedDistanceCm < 0) {
    peer.relativeAngleGuessDeg = 0;
    peer.relativeAngleConfidencePercent = 15;
    peer.lastObservedDistanceCm = peerDominantGateDistanceCm;
    return;
  }

  const int localDelta = localDominantGateDistanceCm - lastLocalDominantGateDistanceCm;
  const int peerDelta = peerDominantGateDistanceCm - peer.lastObservedDistanceCm;
  const bool localMoved = abs(localDelta) >= ROOM_ANGLE_MOTION_THRESHOLD_CM;
  const bool peerMoved = abs(peerDelta) >= ROOM_ANGLE_MOTION_THRESHOLD_CM;

  if (localMoved && peerMoved) {
    if ((localDelta > 0 && peerDelta > 0) || (localDelta < 0 && peerDelta < 0)) {
      peer.parallelScore = min<int>(ROOM_ANGLE_SCORE_LIMIT, peer.parallelScore + 2);
      peer.perpendicularScore = max<int>(-ROOM_ANGLE_SCORE_LIMIT, peer.perpendicularScore - 1);
    } else {
      peer.perpendicularScore = min<int>(ROOM_ANGLE_SCORE_LIMIT, peer.perpendicularScore + 1);
      peer.parallelScore = max<int>(-ROOM_ANGLE_SCORE_LIMIT, peer.parallelScore - 1);
    }
  } else if (localMoved || peerMoved) {
    peer.perpendicularScore = min<int>(ROOM_ANGLE_SCORE_LIMIT, peer.perpendicularScore + 2);
    peer.parallelScore = max<int>(-ROOM_ANGLE_SCORE_LIMIT, peer.parallelScore - 1);
  } else {
    if (peer.parallelScore > 0) {
      peer.parallelScore--;
    } else if (peer.parallelScore < 0) {
      peer.parallelScore++;
    }

    if (peer.perpendicularScore > 0) {
      peer.perpendicularScore--;
    } else if (peer.perpendicularScore < 0) {
      peer.perpendicularScore++;
    }
  }

  const int scoreDifference = peer.perpendicularScore - peer.parallelScore;
  if (scoreDifference >= 2) {
    peer.relativeAngleGuessDeg = 90;
  } else if (scoreDifference <= -2) {
    peer.relativeAngleGuessDeg = 0;
  } else {
    peer.relativeAngleGuessDeg = 45;
  }

  peer.relativeAngleConfidencePercent = static_cast<uint8_t>(constrain(20 + (abs(scoreDifference) * 12), 15, 95));
  peer.lastObservedDistanceCm = peerDominantGateDistanceCm;
}

void appendRoomPeersJson(String& json, int localDominantGateDistanceCm) {
  json += ",\"room_peers\":[";

  const uint32_t now = millis();
  bool firstPeer = true;
  uint8_t visualIndex = 0;
  for (uint8_t i = 0; i < MAX_ROOM_PEERS; i++) {
    RoomPeerSummary& peer = roomPeers[i];
    if (!peer.occupied || (now - peer.lastUpdatedMs) > ROOM_PEER_FRESHNESS_MS) {
      continue;
    }

    if (!firstPeer) {
      json += ",";
    }
    firstPeer = false;

    const int offsetSign = (visualIndex % 2 == 0) ? 1 : -1;
    const int offsetBand = static_cast<int>(visualIndex / 2) + 1;
    const int relativeOffsetXCm = offsetSign * offsetBand * ROOM_RELATIVE_POSE_SPACING_CM;
    visualIndex++;

    json += "{\"node_id\":\"" + jsonEscape(peer.nodeId) + "\"";
    json += ",\"sensor_role\":\"" + jsonEscape(peer.sensorRole) + "\"";
    json += ",\"firmware_version\":\"" + jsonEscape(peer.firmwareVersion) + "\"";
    json += ",\"build_target\":\"" + jsonEscape(peer.buildTarget) + "\"";
    json += ",\"presence\":" + String(peer.presence ? "true" : "false");
    json += ",\"detection_candidate\":" + String(peer.detectionCandidate ? "true" : "false");
    json += ",\"people_estimate\":" + String(peer.peopleEstimate);
    json += ",\"active_gate_count\":" + String(peer.activeGateCount);
    json += ",\"dominant_gate_distance_cm\":" + String(peer.dominantGateDistanceCm);
    json += ",\"activity_score\":" + String(peer.activityScore);
    json += ",\"pose_x_cm\":" + String(peer.poseXCm);
    json += ",\"pose_y_cm\":" + String(peer.poseYCm);
    json += ",\"heading_deg\":" + String(peer.headingDeg);
    json += ",\"room_width_cm\":" + String(peer.roomWidthCm);
    json += ",\"room_height_cm\":" + String(peer.roomHeightCm);
    json += ",\"relative_angle_guess_deg\":" + String(peer.relativeAngleGuessDeg);
    json += ",\"relative_angle_confidence_percent\":" + String(peer.relativeAngleConfidencePercent);
    json += ",\"relative_offset_x_cm\":" + String(relativeOffsetXCm);
    json += ",\"relative_offset_y_cm\":0";
    json += ",\"distance_delta_cm\":" + String((peer.dominantGateDistanceCm >= 0 && localDominantGateDistanceCm >= 0) ? abs(peer.dominantGateDistanceCm - localDominantGateDistanceCm) : -1);
    json += ",\"freshness_ms\":" + String(now - peer.lastUpdatedMs);
    json += "}";
  }

  json += "]";
}

void announceUdpDiscovery(bool emitEvent) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (!udpDiscoveryStarted) {
    udpDiscoveryStarted = DeviceUdpDiscovery.begin(UDP_DISCOVERY_PORT);
    if (!udpDiscoveryStarted) {
      return;
    }
  }

  const IPAddress broadcastIp = localBroadcastIp();
  String payload = String("{") +
                   "\"kind\":\"lb_udp_discovery\"," +
                   "\"node_id\":\"" + jsonEscape(runtimeConfig.nodeId) + "\"," +
                   "\"friendly_name\":\"" + jsonEscape(runtimeConfig.friendlyName) + "\"," +
                   "\"room_id\":\"" + jsonEscape(runtimeConfig.roomId) + "\"," +
                   "\"sensor_role\":\"" + jsonEscape(runtimeConfig.sensorRole) + "\"," +
                   "\"firmware_version\":\"" + jsonEscape(firmwareVersion()) + "\"," +
                   "\"build_target\":\"" + jsonEscape(firmwareBuildTarget()) + "\"," +
                   "\"hostname\":\"" + jsonEscape(deviceHostname()) + "\"," +
                   "\"ip_address\":\"" + jsonEscape(WiFi.localIP().toString()) + "\"," +
                   "\"wifi_rssi_dbm\":" + String(WiFi.RSSI()) + "," +
                   "\"wifi_channel\":" + String(WiFi.channel()) + "," +
                   "\"uptime_s\":" + String(millis() / 1000UL) + "," +
                   "\"free_heap_bytes\":" + String(ESP.getFreeHeap()) +
                   "}";

  if (DeviceUdpDiscovery.beginPacket(broadcastIp, UDP_DISCOVERY_PORT)) {
    DeviceUdpDiscovery.print(payload);
    DeviceUdpDiscovery.endPacket();
    lastUdpDiscoveryAnnounceMs = millis();
  }

  if (emitEvent) {
    printJsonEventPrefix("udp_scan_sent");
    Serial.print(",\"broadcast_ip\":");
    printJsonString(broadcastIp.toString());
    Serial.print(",\"port\":");
    Serial.print(UDP_DISCOVERY_PORT);
    Serial.print(",\"peer_count\":");
    Serial.print(activeUdpDiscoveryPeerCount());
    finishJsonEvent();
  }
}

void serviceUdpDiscovery() {
  if (WiFi.status() != WL_CONNECTED) {
    if (udpDiscoveryStarted) {
      DeviceUdpDiscovery.stop();
      udpDiscoveryStarted = false;
    }

    for (uint8_t index = 0; index < MAX_UDP_DISCOVERY_PEERS; index++) {
      udpDiscoveryPeers[index].occupied = false;
    }
    return;
  }

  if (!udpDiscoveryStarted) {
    udpDiscoveryStarted = DeviceUdpDiscovery.begin(UDP_DISCOVERY_PORT);
    if (udpDiscoveryStarted) {
      lastUdpDiscoveryAnnounceMs = 0;
    }
  }

  int packetSize = DeviceUdpDiscovery.parsePacket();
  while (packetSize > 0) {
    String payload;
    payload.reserve(static_cast<size_t>(packetSize));
    while (packetSize-- > 0) {
      payload += static_cast<char>(DeviceUdpDiscovery.read());
    }

    if (jsonFieldString(payload, "kind") == "lb_udp_discovery") {
      const String nodeId = jsonFieldString(payload, "node_id");
      if (nodeId.length() > 0 && nodeId != runtimeConfig.nodeId) {
        const uint32_t now = millis();
        int slot = -1;
        for (uint8_t index = 0; index < MAX_UDP_DISCOVERY_PEERS; index++) {
          if (udpDiscoveryPeers[index].occupied && udpDiscoveryPeers[index].nodeId == nodeId) {
            slot = index;
            break;
          }
          if (!udpDiscoveryPeers[index].occupied && slot < 0) {
            slot = index;
          }
        }

        if (slot < 0) {
          slot = 0;
          for (uint8_t index = 1; index < MAX_UDP_DISCOVERY_PEERS; index++) {
            if (udpDiscoveryPeers[index].lastSeenMs < udpDiscoveryPeers[slot].lastSeenMs) {
              slot = index;
            }
          }
        }

        UdpDiscoveryPeer& peer = udpDiscoveryPeers[slot];
        const bool wasOccupied = peer.occupied;
        const String previousVersion = peer.firmwareVersion;
        peer.occupied = true;
        peer.nodeId = nodeId;
        peer.friendlyName = jsonFieldString(payload, "friendly_name");
        peer.roomId = jsonFieldString(payload, "room_id");
        peer.sensorRole = jsonFieldString(payload, "sensor_role");
        peer.firmwareVersion = jsonFieldString(payload, "firmware_version");
        peer.buildTarget = jsonFieldString(payload, "build_target");
        peer.hostname = jsonFieldString(payload, "hostname");
        peer.ipAddress = jsonFieldString(payload, "ip_address");
        if (peer.ipAddress.length() == 0) {
          peer.ipAddress = DeviceUdpDiscovery.remoteIP().toString();
        }
        peer.wifiRssi = jsonFieldInt(payload, "wifi_rssi_dbm", 0);
        peer.wifiChannel = jsonFieldInt(payload, "wifi_channel", 0);
        peer.uptimeSeconds = static_cast<uint32_t>(max(0, jsonFieldInt(payload, "uptime_s", 0)));
        peer.freeHeapBytes = static_cast<uint32_t>(max(0, jsonFieldInt(payload, "free_heap_bytes", 0)));
        peer.lastSeenMs = now;

        const bool mismatch = peerVersionMismatch(peer.firmwareVersion);
        const bool previousMismatch = peerVersionMismatch(previousVersion);
        if (!wasOccupied || previousVersion != peer.firmwareVersion || previousMismatch != mismatch) {
          emitPeerVersionEvent("udp_discovery", peer.nodeId, peer.firmwareVersion, mismatch);
        }
      }
    }

    packetSize = DeviceUdpDiscovery.parsePacket();
  }

  const uint32_t now = millis();
  if ((now - lastUdpDiscoveryAnnounceMs) >= UDP_DISCOVERY_ANNOUNCE_MS) {
    announceUdpDiscovery(false);
  }
}

void handleRoomSummaryMessage(const String& topic, const String& payload) {
  int nodesIndex = topic.indexOf("/nodes/");
  if (nodesIndex < 0) {
    return;
  }

  int nodeStart = nodesIndex + 7;
  int nodeEnd = topic.indexOf('/', nodeStart);
  if (nodeEnd <= nodeStart) {
    return;
  }

  String peerNodeId = topic.substring(nodeStart, nodeEnd);
  if (peerNodeId == runtimeConfig.nodeId) {
    return;
  }

  int peerSlot = -1;
  for (uint8_t i = 0; i < MAX_ROOM_PEERS; i++) {
    if (roomPeers[i].occupied && roomPeers[i].nodeId == peerNodeId) {
      peerSlot = i;
      break;
    }
    if (!roomPeers[i].occupied && peerSlot < 0) {
      peerSlot = i;
    }
  }

  if (peerSlot < 0) {
    return;
  }

  RoomPeerSummary& peer = roomPeers[peerSlot];
  const bool wasOccupied = peer.occupied;
  const String previousVersion = peer.firmwareVersion;

  peer.occupied = true;
  peer.nodeId = peerNodeId;
  peer.sensorRole = jsonFieldString(payload, "sensor_role");
  peer.firmwareVersion = jsonFieldString(payload, "firmware_version");
  peer.buildTarget = jsonFieldString(payload, "build_target");
  peer.presence = jsonFieldBool(payload, "presence", false);
  peer.detectionCandidate = jsonFieldBool(payload, "detection_candidate", false);
  peer.peopleEstimate = static_cast<uint8_t>(max(0, jsonFieldInt(payload, "people_estimate", 0)));
  peer.activeGateCount = static_cast<uint8_t>(max(0, jsonFieldInt(payload, "active_gate_count", 0)));
  peer.dominantGateDistanceCm = jsonFieldInt(payload, "dominant_gate_distance_cm", -1);
  peer.activityScore = static_cast<uint8_t>(max(0, jsonFieldInt(payload, "activity_score", 0)));
  peer.poseXCm = static_cast<int16_t>(jsonFieldInt(payload, "pose_x_cm", 0));
  peer.poseYCm = static_cast<int16_t>(jsonFieldInt(payload, "pose_y_cm", 0));
  peer.headingDeg = static_cast<int16_t>(jsonFieldInt(payload, "heading_deg", -90));
  peer.roomWidthCm = static_cast<uint16_t>(max(100, jsonFieldInt(payload, "room_width_cm", 600)));
  peer.roomHeightCm = static_cast<uint16_t>(max(100, jsonFieldInt(payload, "room_height_cm", 400)));
  updateRoomPeerAngleHint(peer, buildRadarDerivedMetrics().dominantGateDistanceCm, peer.dominantGateDistanceCm);
  peer.lastUpdatedMs = millis();

  const bool mismatch = peerVersionMismatch(peer.firmwareVersion);
  const bool previousMismatch = peerVersionMismatch(previousVersion);
  if (!wasOccupied || previousVersion != peer.firmwareVersion || previousMismatch != mismatch) {
    emitPeerVersionEvent("room_summary", peer.nodeId, peer.firmwareVersion, mismatch);
  }
}

void applyRoomPoseConfig(const String& roomId,
                         const String& sensorRole,
                         const String& poseXValue,
                         const String& poseYValue,
                         const String& headingValue,
                         const String& roomWidthValue,
                         const String& roomHeightValue,
                         bool resetConnections,
                         bool emitSavedEvent,
                         const char* eventName) {
  runtimeConfig.roomId = roomId.length() > 0 ? roomId : defaultRoomId();
  runtimeConfig.sensorRole = sensorRole.length() > 0 ? sensorRole : defaultSensorRole();

  if (poseXValue.length() > 0) {
    runtimeConfig.roomPoseXCm = static_cast<int16_t>(constrain(poseXValue.toInt(), -2000, 2000));
  }
  if (poseYValue.length() > 0) {
    runtimeConfig.roomPoseYCm = static_cast<int16_t>(constrain(poseYValue.toInt(), -2000, 2000));
  }
  if (headingValue.length() > 0) {
    runtimeConfig.roomHeadingDeg = static_cast<int16_t>(constrain(headingValue.toInt(), -180, 180));
  }
  if (roomWidthValue.length() > 0) {
    runtimeConfig.roomWidthCm = static_cast<uint16_t>(constrain(roomWidthValue.toInt(), 100, 4000));
  }
  if (roomHeightValue.length() > 0) {
    runtimeConfig.roomHeightCm = static_cast<uint16_t>(constrain(roomHeightValue.toInt(), 100, 4000));
  }

  saveHomeAssistantConfig();
  if (resetConnections) {
    resetHomeAssistantConnections();
  }

  if (emitSavedEvent) {
    printJsonEventPrefix(eventName != nullptr ? eventName : "ha_room_config_saved");
    Serial.print(",\"room_id\":");
    printJsonString(runtimeConfig.roomId);
    Serial.print(",\"sensor_role\":");
    printJsonString(runtimeConfig.sensorRole);
    Serial.print(",\"pose_x_cm\":");
    Serial.print(runtimeConfig.roomPoseXCm);
    Serial.print(",\"pose_y_cm\":");
    Serial.print(runtimeConfig.roomPoseYCm);
    Serial.print(",\"heading_deg\":");
    Serial.print(runtimeConfig.roomHeadingDeg);
    Serial.print(",\"room_width_cm\":");
    Serial.print(runtimeConfig.roomWidthCm);
    Serial.print(",\"room_height_cm\":");
    Serial.print(runtimeConfig.roomHeightCm);
    finishJsonEvent();
  }

  emitHomeAssistantConfigEvent();
}

void handleRoomPoseCommandMessage(const String& payload) {
  String targetNodeId = jsonFieldString(payload, "node_id");
  if (targetNodeId.length() > 0 && targetNodeId != runtimeConfig.nodeId) {
    return;
  }

  applyRoomPoseConfig(jsonFieldString(payload, "room_id"),
                      jsonFieldString(payload, "sensor_role"),
                      String(jsonFieldInt(payload, "pose_x_cm", runtimeConfig.roomPoseXCm)),
                      String(jsonFieldInt(payload, "pose_y_cm", runtimeConfig.roomPoseYCm)),
                      String(jsonFieldInt(payload, "heading_deg", runtimeConfig.roomHeadingDeg)),
            String(jsonFieldInt(payload, "room_width_cm", runtimeConfig.roomWidthCm)),
            String(jsonFieldInt(payload, "room_height_cm", runtimeConfig.roomHeightCm)),
                      false,
                      true,
                      "ha_room_config_saved");
}

bool publishRoomPoseCommand(const String& nodeId,
                            const String& roomId,
                            const String& sensorRole,
                            int16_t poseXCm,
                            int16_t poseYCm,
              int16_t headingDeg,
              uint16_t roomWidthCm,
              uint16_t roomHeightCm) {
  if (!HomeAssistantMqttClient.connected() || nodeId.length() == 0) {
    return false;
  }

  String payload = String("{") +
                   "\"node_id\":\"" + jsonEscape(nodeId) + "\"," +
                   "\"room_id\":\"" + jsonEscape(roomId.length() > 0 ? roomId : runtimeConfig.roomId) + "\"," +
                   "\"sensor_role\":\"" + jsonEscape(sensorRole.length() > 0 ? sensorRole : runtimeConfig.sensorRole) + "\"," +
                   "\"pose_x_cm\":" + String(poseXCm) + "," +
                   "\"pose_y_cm\":" + String(poseYCm) + "," +
                   "\"heading_deg\":" + String(headingDeg) + "," +
                   "\"room_width_cm\":" + String(roomWidthCm) + "," +
                   "\"room_height_cm\":" + String(roomHeightCm) +
                   "}";

  return HomeAssistantMqttClient.publish(roomPoseCommandTopic(nodeId).c_str(), payload.c_str(), false);
}

void handleMqttMessage(char* topic, uint8_t* payload, unsigned int length) {
  if (topic == nullptr || payload == nullptr || length == 0) {
    return;
  }

  String topicValue(topic);
  String payloadValue;
  payloadValue.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) {
    payloadValue += static_cast<char>(payload[i]);
  }

  if (topicValue == roomNodeSummaryTopic(runtimeConfig.nodeId)) {
    return;
  }

  if (topicValue == roomPoseCommandTopic(runtimeConfig.nodeId)) {
    handleRoomPoseCommandMessage(payloadValue);
    return;
  }

  if (topicValue.startsWith(roomTopicRoot() + "/nodes/")) {
    handleRoomSummaryMessage(topicValue, payloadValue);
  }
}

bool subscribeRoomTopics() {
  if (!HomeAssistantMqttClient.connected()) {
    return false;
  }

  bool summarySubscribed = HomeAssistantMqttClient.subscribe(roomSummarySubscriptionTopic().c_str());
  bool poseSubscribed = HomeAssistantMqttClient.subscribe(roomPoseCommandSubscriptionTopic().c_str());
  return summarySubscribed && poseSubscribed;
}

void publishRoomCollaborationSummary() {
  if (!HomeAssistantMqttClient.connected()) {
    return;
  }

  RadarDerivedMetrics metrics = buildRadarDerivedMetrics();
  const bool detectionCandidate = radarDetectionCandidate(metrics);
  const uint32_t now = millis();

  const bool distanceChanged = (lastPublishedRoomSummary.dominantGateDistanceCm < 0) != (metrics.dominantGateDistanceCm < 0) ||
                               (metrics.dominantGateDistanceCm >= 0 && lastPublishedRoomSummary.dominantGateDistanceCm >= 0 &&
                                abs(metrics.dominantGateDistanceCm - lastPublishedRoomSummary.dominantGateDistanceCm) >= ROOM_SUMMARY_DISTANCE_DELTA_CM);
  const bool activityChanged = abs(static_cast<int>(metrics.activityScore) - static_cast<int>(lastPublishedRoomSummary.activityScore)) >= ROOM_SUMMARY_ACTIVITY_DELTA;
  const bool geometryChanged = runtimeConfig.roomPoseXCm != lastPublishedRoomSummary.poseXCm ||
                               runtimeConfig.roomPoseYCm != lastPublishedRoomSummary.poseYCm ||
                               runtimeConfig.roomHeadingDeg != lastPublishedRoomSummary.headingDeg ||
                               runtimeConfig.roomWidthCm != lastPublishedRoomSummary.roomWidthCm ||
                               runtimeConfig.roomHeightCm != lastPublishedRoomSummary.roomHeightCm;

  if (lastPublishedRoomSummary.valid &&
      (now - lastPublishedRoomSummary.publishedAtMs) < ROOM_SUMMARY_KEEPALIVE_MS &&
      lastPublishedRoomSummary.presence == lastPresence &&
      lastPublishedRoomSummary.detectionCandidate == detectionCandidate &&
      lastPublishedRoomSummary.peopleEstimate == metrics.estimatedPeople &&
      lastPublishedRoomSummary.activeGateCount == metrics.activeGateCount &&
      !distanceChanged &&
      !activityChanged &&
      !geometryChanged) {
    return;
  }

  String payload = String("{") +
                   "\"node_id\":\"" + jsonEscape(runtimeConfig.nodeId) + "\"," +
                   "\"room_id\":\"" + jsonEscape(runtimeConfig.roomId) + "\"," +
                   "\"sensor_role\":\"" + jsonEscape(runtimeConfig.sensorRole) + "\"," +
                   "\"firmware_version\":\"" + jsonEscape(firmwareVersion()) + "\"," +
                   "\"pose_x_cm\":" + String(runtimeConfig.roomPoseXCm) + "," +
                   "\"pose_y_cm\":" + String(runtimeConfig.roomPoseYCm) + "," +
                   "\"heading_deg\":" + String(runtimeConfig.roomHeadingDeg) + "," +
                   "\"room_width_cm\":" + String(runtimeConfig.roomWidthCm) + "," +
                   "\"room_height_cm\":" + String(runtimeConfig.roomHeightCm) + "," +
                   "\"presence\":" + String(lastPresence ? "true" : "false") + "," +
                   "\"detection_candidate\":" + String(detectionCandidate ? "true" : "false") + "," +
                   "\"people_estimate\":" + String(metrics.estimatedPeople) + "," +
                   "\"active_gate_count\":" + String(metrics.activeGateCount) + "," +
                   "\"dominant_gate_distance_cm\":" + String(metrics.dominantGateDistanceCm) + "," +
                   "\"activity_score\":" + String(metrics.activityScore) + "," +
                   "\"updated_ms\":" + String(now) +
                   "}";

  lastLocalDominantGateDistanceCm = metrics.dominantGateDistanceCm;
  lastPublishedRoomSummary.valid = true;
  lastPublishedRoomSummary.presence = lastPresence;
  lastPublishedRoomSummary.detectionCandidate = detectionCandidate;
  lastPublishedRoomSummary.peopleEstimate = metrics.estimatedPeople;
  lastPublishedRoomSummary.activeGateCount = metrics.activeGateCount;
  lastPublishedRoomSummary.dominantGateDistanceCm = metrics.dominantGateDistanceCm;
  lastPublishedRoomSummary.activityScore = metrics.activityScore;
  lastPublishedRoomSummary.poseXCm = runtimeConfig.roomPoseXCm;
  lastPublishedRoomSummary.poseYCm = runtimeConfig.roomPoseYCm;
  lastPublishedRoomSummary.headingDeg = runtimeConfig.roomHeadingDeg;
  lastPublishedRoomSummary.roomWidthCm = runtimeConfig.roomWidthCm;
  lastPublishedRoomSummary.roomHeightCm = runtimeConfig.roomHeightCm;
  lastPublishedRoomSummary.publishedAtMs = now;

  String topic = roomNodeSummaryTopic(runtimeConfig.nodeId);
  HomeAssistantMqttClient.publish(topic.c_str(), payload.c_str(), true);
}

RoomAggregateMetrics buildRoomAggregateMetrics() {
  RoomAggregateMetrics metrics;
  RadarDerivedMetrics localMetrics = buildRadarDerivedMetrics();
  const bool localDetectionCandidate = radarDetectionCandidate(localMetrics);
  const uint8_t localQualifiedPeople = localDetectionCandidate ? max<uint8_t>(1, localMetrics.estimatedPeople) : 0;
  const uint32_t now = millis();
  uint8_t peakPeople = localQualifiedPeople;
  uint8_t totalPeople = localQualifiedPeople;
  uint16_t totalActivity = localMetrics.activityScore;
  uint8_t unresolvedPeople = 0;

  struct SensorPose {
    const char* nodeId;
    int16_t x;
    int16_t y;
    int16_t headingDeg;
  };

  struct DetectionCluster {
    float x;
    float y;
    uint8_t members;
  };

  SensorPose sensorPoses[MAX_ROOM_PEERS + 1] = {};
  uint8_t sensorPoseCount = 0;
  sensorPoses[sensorPoseCount++] = {runtimeConfig.nodeId.c_str(), runtimeConfig.roomPoseXCm, runtimeConfig.roomPoseYCm, runtimeConfig.roomHeadingDeg};

  for (uint8_t i = 0; i < MAX_ROOM_PEERS && sensorPoseCount < (MAX_ROOM_PEERS + 1); i++) {
    RoomPeerSummary& peer = roomPeers[i];
    if (!peer.occupied || (now - peer.lastUpdatedMs) > ROOM_PEER_FRESHNESS_MS) {
      continue;
    }
    sensorPoses[sensorPoseCount++] = {peer.nodeId.c_str(), peer.poseXCm, peer.poseYCm, peer.headingDeg};
  }

  DetectionCluster clusters[MAX_ROOM_PEERS + 1] = {};
  uint8_t clusterCount = 0;

  auto addDetectionCluster = [&](const char* originNodeId,
                                 uint8_t estimatedPeople,
                                 int dominantDistanceCm,
                                 int16_t poseXCm,
                                 int16_t poseYCm,
                                 int16_t headingDeg,
                                 uint16_t roomWidthCm,
                                 uint16_t roomHeightCm) {
    if (estimatedPeople > 1) {
      unresolvedPeople = static_cast<uint8_t>(min<int>(255, unresolvedPeople + (estimatedPeople - 1)));
    }

    if (dominantDistanceCm < 0) {
      if (estimatedPeople > 0) {
        unresolvedPeople = static_cast<uint8_t>(min<int>(255, unresolvedPeople + 1));
      }
      return;
    }

    const float headingRad = static_cast<float>(headingDeg) * DEG_TO_RAD;
    const float detectionX = static_cast<float>(poseXCm) + cosf(headingRad) * static_cast<float>(dominantDistanceCm);
    const float detectionY = static_cast<float>(poseYCm) + sinf(headingRad) * static_cast<float>(dominantDistanceCm);
    const float minX = -static_cast<float>(ROOM_FUSION_ROOM_MARGIN_CM);
    const float minY = -static_cast<float>(ROOM_FUSION_ROOM_MARGIN_CM);
    const float maxX = static_cast<float>(roomWidthCm) + static_cast<float>(ROOM_FUSION_ROOM_MARGIN_CM);
    const float maxY = static_cast<float>(roomHeightCm) + static_cast<float>(ROOM_FUSION_ROOM_MARGIN_CM);

    if (detectionX < minX || detectionX > maxX || detectionY < minY || detectionY > maxY) {
      return;
    }

    for (uint8_t poseIndex = 0; poseIndex < sensorPoseCount; poseIndex++) {
      const SensorPose& pose = sensorPoses[poseIndex];
      if (originNodeId != nullptr && String(originNodeId) == String(pose.nodeId)) {
        continue;
      }

      const float dx = detectionX - static_cast<float>(pose.x);
      const float dy = detectionY - static_cast<float>(pose.y);
      if (sqrtf((dx * dx) + (dy * dy)) <= static_cast<float>(ROOM_SENSOR_GHOST_RADIUS_CM)) {
        return;
      }
    }

    for (uint8_t clusterIndex = 0; clusterIndex < clusterCount; clusterIndex++) {
      DetectionCluster& cluster = clusters[clusterIndex];
      const float dx = detectionX - cluster.x;
      const float dy = detectionY - cluster.y;
      if (sqrtf((dx * dx) + (dy * dy)) <= static_cast<float>(ROOM_FUSION_CLUSTER_RADIUS_CM)) {
        cluster.x = ((cluster.x * static_cast<float>(cluster.members)) + detectionX) / static_cast<float>(cluster.members + 1);
        cluster.y = ((cluster.y * static_cast<float>(cluster.members)) + detectionY) / static_cast<float>(cluster.members + 1);
        cluster.members++;
        return;
      }
    }

    if (clusterCount < (MAX_ROOM_PEERS + 1)) {
      clusters[clusterCount++] = {detectionX, detectionY, 1};
      return;
    }

    unresolvedPeople = static_cast<uint8_t>(min<int>(255, unresolvedPeople + 1));
  };

  if (localDetectionCandidate) {
    metrics.activeNodeCount++;
    addDetectionCluster(runtimeConfig.nodeId.c_str(),
                        localQualifiedPeople,
                        localMetrics.dominantGateDistanceCm,
                        runtimeConfig.roomPoseXCm,
                        runtimeConfig.roomPoseYCm,
                        runtimeConfig.roomHeadingDeg,
                        runtimeConfig.roomWidthCm,
                        runtimeConfig.roomHeightCm);
  }

  for (uint8_t i = 0; i < MAX_ROOM_PEERS; i++) {
    RoomPeerSummary& peer = roomPeers[i];
    if (!peer.occupied || (now - peer.lastUpdatedMs) > ROOM_PEER_FRESHNESS_MS) {
      continue;
    }

    metrics.peerNodeCount++;
    totalActivity += peer.activityScore;
    const uint8_t peerQualifiedPeople = peer.detectionCandidate ? max<uint8_t>(1, peer.peopleEstimate) : 0;
    peakPeople = max<uint8_t>(peakPeople, peerQualifiedPeople);
    totalPeople = static_cast<uint8_t>(min<int>(255, totalPeople + peerQualifiedPeople));

    if (peer.detectionCandidate) {
      metrics.activeNodeCount++;
      addDetectionCluster(peer.nodeId.c_str(),
                          peerQualifiedPeople,
                          peer.dominantGateDistanceCm,
                          peer.poseXCm,
                          peer.poseYCm,
                          peer.headingDeg,
                          peer.roomWidthCm,
                          peer.roomHeightCm);
    }
  }

  const uint8_t fusedPeopleEstimate = static_cast<uint8_t>(min<int>(255, clusterCount + unresolvedPeople));
  metrics.peopleEstimate = max<uint8_t>(peakPeople, fusedPeopleEstimate);
  metrics.peopleEstimate = min<uint8_t>(metrics.peopleEstimate, totalPeople);
  metrics.activityScore = static_cast<uint8_t>(min<uint16_t>(100, totalActivity));
  return metrics;
}

uint16_t effectiveMinGateEnergy() {
  return effectiveMinGateEnergy(runtimeConfig);
}

uint16_t effectiveMinGateEnergy(const RuntimeHomeAssistantConfig& config) {
  uint8_t sensitivity = constrain(config.sensitivityPercent, static_cast<uint8_t>(10), static_cast<uint8_t>(100));
  uint32_t scaled = (static_cast<uint32_t>(config.minGateEnergy) * static_cast<uint32_t>(150 - sensitivity)) / 100U;
  return static_cast<uint16_t>(max<uint32_t>(10U, scaled));
}

uint8_t effectiveMinActivityScore() {
  return effectiveMinActivityScore(runtimeConfig);
}

uint8_t effectiveMinActivityScore(const RuntimeHomeAssistantConfig& config) {
  uint8_t sensitivity = constrain(config.sensitivityPercent, static_cast<uint8_t>(10), static_cast<uint8_t>(100));
  uint32_t scaled = (static_cast<uint32_t>(config.minActivityScore) * static_cast<uint32_t>(150 - sensitivity)) / 100U;
  return static_cast<uint8_t>(max<uint32_t>(1U, min<uint32_t>(100U, scaled)));
}

bool energyFrameLooksLikeNearFieldClutter(const RadarDerivedMetrics& metrics, const LatestEnergyFrameSnapshot& energyFrame) {
  if (!metrics.energyBased || !energyFrame.valid || metrics.dominantGateIndex < 0) {
    return false;
  }

  if (metrics.dominantGateIndex > LD2420_NEAR_FIELD_CLUTTER_MAX_GATE_INDEX || metrics.totalGateEnergy == 0) {
    return false;
  }

  const int reportedDistanceCm = energyFrame.distanceCm;
  if (reportedDistanceCm <= 0 || metrics.dominantGateDistanceCm < 0) {
    return false;
  }

  if ((reportedDistanceCm - metrics.dominantGateDistanceCm) < LD2420_NEAR_FIELD_CLUTTER_DISTANCE_DELTA_CM) {
    return false;
  }

  uint32_t nearFieldBandEnergy = 0;
  for (uint8_t gateIndex = 0; gateIndex < min<uint8_t>(LD2420_GATE_COUNT, LD2420_NEAR_FIELD_CLUTTER_BAND_GATES); gateIndex++) {
    nearFieldBandEnergy += energyFrame.gates[gateIndex];
  }

  const uint32_t nearFieldBandSharePercent = (nearFieldBandEnergy * 100UL) / metrics.totalGateEnergy;
  if (nearFieldBandSharePercent >= LD2420_NEAR_FIELD_CLUTTER_BAND_SHARE_PERCENT) {
    return true;
  }

  const uint32_t peakSharePercent = (static_cast<uint32_t>(metrics.dominantGateEnergy) * 100UL) / metrics.totalGateEnergy;
  if (peakSharePercent < LD2420_NEAR_FIELD_CLUTTER_PEAK_SHARE_PERCENT) {
    return false;
  }

  int reportedGateIndex = (reportedDistanceCm + (LD2420_GATE_SIZE_CM / 2)) / LD2420_GATE_SIZE_CM;
  reportedGateIndex = constrain(reportedGateIndex, 0, LD2420_GATE_COUNT - 1);

  uint32_t reportedNeighborhoodEnergy = energyFrame.gates[reportedGateIndex];
  if (reportedGateIndex > 0) {
    reportedNeighborhoodEnergy += energyFrame.gates[reportedGateIndex - 1];
  }
  if (reportedGateIndex < (LD2420_GATE_COUNT - 1)) {
    reportedNeighborhoodEnergy += energyFrame.gates[reportedGateIndex + 1];
  }

  return metrics.dominantGateEnergy > reportedNeighborhoodEnergy;
}

bool radarDetectionCandidate(const RadarDerivedMetrics& metrics) {
  return radarDetectionDecision(metrics,
                                latestEnergyFrameSnapshot.valid ? &latestEnergyFrameSnapshot : nullptr,
                                runtimeConfig) == RadarDetectionDecision::Candidate;
}

bool radarDetectionCandidate(const RadarDerivedMetrics& metrics,
                             const LatestEnergyFrameSnapshot* energyFrame,
                             const RuntimeHomeAssistantConfig& config) {
  return radarDetectionDecision(metrics, energyFrame, config) == RadarDetectionDecision::Candidate;
}

RadarDetectionDecision radarDetectionDecision(const RadarDerivedMetrics& metrics) {
  return radarDetectionDecision(metrics,
                                latestEnergyFrameSnapshot.valid ? &latestEnergyFrameSnapshot : nullptr,
                                runtimeConfig);
}

RadarDetectionDecision radarDetectionDecision(const RadarDerivedMetrics& metrics,
                                              const LatestEnergyFrameSnapshot* energyFrame,
                                              const RuntimeHomeAssistantConfig& config) {
  if (!metrics.valid) {
    return RadarDetectionDecision::InvalidMetrics;
  }

  if (metrics.dominantGateDistanceCm >= 0 && metrics.dominantGateDistanceCm > config.maxDetectionRangeCm) {
    return RadarDetectionDecision::OutOfRange;
  }

  if (metrics.activeGateCount < config.minActiveGates) {
    return RadarDetectionDecision::InsufficientActiveGates;
  }

  const uint8_t minActivity = effectiveMinActivityScore(config);
  if (metrics.activityScore < minActivity) {
    return RadarDetectionDecision::LowActivity;
  }

  if (metrics.energyBased) {
    if (energyFrame == nullptr) {
      return RadarDetectionDecision::MissingEnergyFrame;
    }

    uint16_t minEnergy = effectiveMinGateEnergy(config);
    uint32_t minTotalEnergy = static_cast<uint32_t>(minEnergy) * max<uint8_t>(config.minActiveGates, static_cast<uint8_t>(1));
    if (metrics.dominantGateEnergy < minEnergy && metrics.totalGateEnergy < minTotalEnergy) {
      return RadarDetectionDecision::LowEnergy;
    }

    if (energyFrameLooksLikeNearFieldClutter(metrics, *energyFrame)) {
      return RadarDetectionDecision::NearFieldClutter;
    }
  }

  return (metrics.estimatedPeople > 0 || metrics.activityScore >= minActivity)
    ? RadarDetectionDecision::Candidate
    : RadarDetectionDecision::LowActivity;
}

uint16_t effectivePresenceHoldMs() {
  return static_cast<uint16_t>(constrain(runtimeConfig.presenceHoldMs,
                                         MIN_PRESENCE_HOLD_MS,
                                         MAX_PRESENCE_HOLD_MS));
}

uint32_t presenceDecayRemainingMs(uint32_t now) {
  uint16_t holdMs = effectivePresenceHoldMs();
  if (holdMs == 0 || lastDetectionMs == 0) {
    return 0;
  }

  uint32_t elapsed = now - lastDetectionMs;
  if (elapsed >= holdMs) {
    return 0;
  }

  return holdMs - elapsed;
}

uint32_t statusLedColorForState(uint32_t now) {
  if (!runtimeConfig.ledEnabled) {
    return StatusRgbLed.Color(0, 0, 0);
  }

  if (detectionCandidateActive) {
    return StatusRgbLed.Color(0, runtimeConfig.ledBrightness, 0);
  }

  uint16_t holdMs = effectivePresenceHoldMs();
  uint32_t remaining = presenceDecayRemainingMs(now);
  if (remaining == 0 || holdMs == 0) {
    return StatusRgbLed.Color(runtimeConfig.ledBrightness, 0, 0);
  }

  uint32_t green = (static_cast<uint32_t>(runtimeConfig.ledBrightness) * remaining) / holdMs;
  uint32_t red = runtimeConfig.ledBrightness - green;
  return StatusRgbLed.Color(static_cast<uint8_t>(red), static_cast<uint8_t>(green), 0);
}

String statusLedHex(uint32_t color) {
  char buffer[8];
  uint8_t red = static_cast<uint8_t>(color >> 16);
  uint8_t green = static_cast<uint8_t>(color >> 8);
  uint8_t blue = static_cast<uint8_t>(color);
  snprintf(buffer, sizeof(buffer), "%02X%02X%02X", red, green, blue);
  return String(buffer);
}

void updateStatusRgbLed(uint32_t now) {
  if (!kBoardProfile.hasStatusRgbLed) {
    return;
  }

  StatusRgbLed.setBrightness(runtimeConfig.ledBrightness);
  StatusRgbLed.setPixelColor(0, statusLedColorForState(now));
  StatusRgbLed.show();
}

void publishHomeAssistantAvailability(bool online) {
  if (!HomeAssistantMqttClient.connected()) {
    return;
  }

  String topic = homeAssistantDeviceTopic() + "/availability";
  HomeAssistantMqttClient.publish(topic.c_str(), online ? "online" : "offline", true);
}

RadarDerivedMetrics buildRadarDerivedMetrics() {
  return buildRadarDerivedMetrics(latestEnergyFrameSnapshot.valid ? &latestEnergyFrameSnapshot : nullptr,
                                  latestTextFrameSnapshot.valid ? &latestTextFrameSnapshot : nullptr,
                                  presenceInitialized ? lastGpioPresence : false);
}

RadarDerivedMetrics buildRadarDerivedMetrics(const LatestEnergyFrameSnapshot* energyFrame,
                                            const LatestTextFrameSnapshot* textFrame,
                                            bool gpioPresence) {
  RadarDerivedMetrics metrics;
  metrics.valid = energyFrame != nullptr || textFrame != nullptr || gpioPresence;

  if (energyFrame != nullptr) {
    metrics.energyBased = true;

    uint16_t peakEnergy = 0;
    int peakIndex = -1;
    uint8_t clusterCount = 0;
    bool clusterOpen = false;

    for (uint8_t gateIndex = 0; gateIndex < LD2420_GATE_COUNT; gateIndex++) {
      const uint16_t gateEnergy = energyFrame->gates[gateIndex];
      metrics.totalGateEnergy += gateEnergy;

      if (gateEnergy > peakEnergy) {
        peakEnergy = gateEnergy;
        peakIndex = gateIndex;
      }
    }

    const uint16_t activeThreshold = peakEnergy > 0
      ? max<uint16_t>(LD2420_ACTIVE_GATE_FLOOR, static_cast<uint16_t>(peakEnergy / 5U))
      : LD2420_ACTIVE_GATE_FLOOR;

    for (uint8_t gateIndex = 0; gateIndex < LD2420_GATE_COUNT; gateIndex++) {
      const bool gateActive = energyFrame->gates[gateIndex] >= activeThreshold;

      if (gateActive) {
        metrics.activeGateCount++;
      }

      if (gateActive && !clusterOpen) {
        clusterCount++;
        clusterOpen = true;
      } else if (!gateActive) {
        clusterOpen = false;
      }
    }

    metrics.dominantGateIndex = peakIndex;
    metrics.dominantGateEnergy = peakEnergy;
    if (peakIndex >= 0) {
      metrics.dominantGateDistanceCm = (peakIndex * LD2420_GATE_SIZE_CM) + (LD2420_GATE_SIZE_CM / 2);
    }

    if (energyFrame->presence && peakEnergy > 0) {
      uint8_t estimatedPeople = max<uint8_t>(1, clusterCount);
      if (metrics.activeGateCount >= 8 && metrics.totalGateEnergy >= (static_cast<uint32_t>(peakEnergy) * 3UL)) {
        estimatedPeople++;
      }
      metrics.estimatedPeople = min<uint8_t>(LD2420_MAX_ESTIMATED_PEOPLE, estimatedPeople);
    }

    uint32_t activityScore = (metrics.totalGateEnergy / 300UL) +
                             (static_cast<uint32_t>(metrics.activeGateCount) * 6UL) +
                             (static_cast<uint32_t>(peakEnergy) / 120UL);
    metrics.activityScore = static_cast<uint8_t>(min<uint32_t>(100UL, activityScore));

    if (energyFrameLooksLikeNearFieldClutter(metrics, *energyFrame)) {
      metrics.estimatedPeople = 0;
    }

    return metrics;
  }

  if (textFrame != nullptr) {
    metrics.estimatedPeople = textFrame->presence ? 1 : 0;
    metrics.activeGateCount = textFrame->presence ? 1 : 0;
    metrics.activityScore = textFrame->presence ? 15 : 0;

    if (textFrame->range >= 0) {
      metrics.dominantGateDistanceCm = textFrame->range;
      metrics.dominantGateIndex = min<int>(LD2420_GATE_COUNT - 1, textFrame->range / LD2420_GATE_SIZE_CM);
    }

    return metrics;
  }

  metrics.estimatedPeople = gpioPresence ? 1 : 0;
  metrics.activeGateCount = gpioPresence ? 1 : 0;
  metrics.activityScore = gpioPresence ? 5 : 0;
  return metrics;
}

String buildHomeAssistantSensorPayload(const String& name,
                                       const String& uniqueId,
                                       const String& stateTopic,
                                       const String& availabilityTopic,
                                       const String& deviceJson,
                                       const String& extraFields) {
  String payload = String("{") +
                   "\"name\":\"" + name + "\"," +
                   "\"uniq_id\":\"" + uniqueId + "\"," +
                   "\"stat_t\":\"" + stateTopic + "\",";

  if (extraFields.length() > 0) {
    payload += extraFields;
    if (!extraFields.endsWith(",")) {
      payload += ",";
    }
  }

  payload += "\"avty_t\":\"" + availabilityTopic + "\"," +
             "\"pl_avail\":\"online\"," +
             "\"pl_not_avail\":\"offline\"," +
             deviceJson +
             "}";
  return payload;
}

void publishHomeAssistantPresence() {
  if (!HomeAssistantMqttClient.connected()) {
    return;
  }

  String topic = homeAssistantStateTopic("presence");
  HomeAssistantMqttClient.publish(topic.c_str(), lastPresence ? "ON" : "OFF", true);
}

void publishHomeAssistantDistance() {
  if (!HomeAssistantMqttClient.connected() || lastDistanceCm < 0) {
    return;
  }

  char payload[16];
  snprintf(payload, sizeof(payload), "%d", lastDistanceCm);

  String topic = homeAssistantStateTopic("distance_cm");
  HomeAssistantMqttClient.publish(topic.c_str(), payload, true);
}

void publishObservationFeed() {
  if (!HomeAssistantMqttClient.connected()) {
    return;
  }

  const uint32_t now = millis();
  const uint32_t uptimeSeconds = now / 1000UL;
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  const uint32_t freeHeapBytes = ESP.getFreeHeap();
  RadarDerivedMetrics radarMetrics = buildRadarDerivedMetrics();
  RoomAggregateMetrics roomMetrics = buildRoomAggregateMetrics();
  const bool detectionCandidate = radarDetectionCandidate(radarMetrics);

  String payload = "{";
  payload += "\"node_id\":\"" + jsonEscape(runtimeConfig.nodeId) + "\"";
  payload += ",\"friendly_name\":\"" + jsonEscape(runtimeConfig.friendlyName) + "\"";
  payload += ",\"firmware_version\":\"" + jsonEscape(firmwareVersion()) + "\"";
  payload += ",\"build_target\":\"" + jsonEscape(firmwareBuildTarget()) + "\"";
  payload += ",\"room_id\":\"" + jsonEscape(runtimeConfig.roomId) + "\"";
  payload += ",\"sensor_role\":\"" + jsonEscape(runtimeConfig.sensorRole) + "\"";
  payload += ",\"device_hostname\":\"" + jsonEscape(deviceHostname()) + "\"";
  payload += ",\"dashboard_url\":\"" + jsonEscape(deviceDashboardUrl()) + "\"";
  payload += ",\"uptime_s\":" + String(uptimeSeconds);
  payload += ",\"free_heap_bytes\":" + String(freeHeapBytes);
  payload += ",\"wifi\":{";
  payload += "\"connected\":";
  payload += wifiConnected ? "true" : "false";
  payload += ",\"ip_address\":\"" + jsonEscape(wifiConnected ? WiFi.localIP().toString() : String("0.0.0.0")) + "\"";
  payload += ",\"rssi_dbm\":" + String(wifiConnected ? WiFi.RSSI() : 0);
  payload += ",\"channel\":" + String(wifiConnected ? WiFi.channel() : 0);
  payload += "}";
  payload += ",\"radar\":{";
  payload += "\"presence\":";
  payload += lastPresence ? "true" : "false";
  payload += ",\"gpio_presence\":";
  payload += lastGpioPresence ? "true" : "false";
  payload += ",\"detection_candidate\":";
  payload += detectionCandidate ? "true" : "false";
  payload += ",\"people_estimate\":" + String(radarMetrics.estimatedPeople);
  payload += ",\"active_gate_count\":" + String(radarMetrics.activeGateCount);
  payload += ",\"activity_score\":" + String(radarMetrics.activityScore);
  payload += ",\"dominant_gate_distance_cm\":" + String(radarMetrics.dominantGateDistanceCm);
  payload += ",\"dominant_gate_energy\":" + String(radarMetrics.dominantGateEnergy);
  payload += ",\"total_gate_energy\":" + String(radarMetrics.totalGateEnergy);
  payload += "}";
  payload += ",\"room\":{";
  payload += "\"people_estimate\":" + String(roomMetrics.peopleEstimate);
  payload += ",\"active_nodes\":" + String(roomMetrics.activeNodeCount);
  payload += ",\"peer_nodes\":" + String(roomMetrics.peerNodeCount);
  payload += ",\"activity_score\":" + String(roomMetrics.activityScore);
  payload += "}";
  payload += ",\"ble\":{";
  payload += "\"beacon_count\":" + String(activeBleSightingCount());
  payload += ",\"tagged_people_count\":" + String(activeBleTagCount());
  payload += ",\"beacons\":[";

  bool firstBeacon = true;
  for (uint8_t index = 0; index < MAX_BLE_SIGHTINGS; index++) {
    BleBeaconSighting& sighting = bleSightings[index];
    if (!sighting.occupied || (now - sighting.lastSeenMs) > BLE_SIGHTING_FRESHNESS_MS) {
      continue;
    }
    if (!firstBeacon) {
      payload += ",";
    }
    firstBeacon = false;
    payload += "{\"address\":\"" + jsonEscape(sighting.address) + "\"";
    payload += ",\"name\":\"" + jsonEscape(sighting.name) + "\"";
    payload += ",\"service_uuid\":\"" + jsonEscape(sighting.serviceUuid) + "\"";
    payload += ",\"rssi\":" + String(sighting.rssi);
    payload += ",\"age_ms\":" + String(now - sighting.lastSeenMs);
    payload += "}";
  }

  payload += "],\"tags\":[";

  bool firstTag = true;
  for (uint8_t index = 0; index < MAX_BLE_TAGS; index++) {
    const BleIdentityTag& tag = bleIdentityTags[index];
    if (!tag.occupied) {
      continue;
    }
    if (!firstTag) {
      payload += ",";
    }
    firstTag = false;
    const bool present = bleIdentityTagPresent(tag, now);
    payload += "{\"slot\":" + String(index);
    payload += ",\"label\":\"" + jsonEscape(tag.label) + "\"";
    payload += ",\"address\":\"" + jsonEscape(tag.address) + "\"";
    payload += ",\"present\":";
    payload += present ? "true" : "false";
    payload += ",\"rssi\":" + String(present ? tag.lastRssi : -127);
    payload += ",\"age_ms\":" + String(tag.lastSeenMs > 0 ? (now - tag.lastSeenMs) : BLE_TAG_FRESHNESS_MS + 1UL);
    payload += "}";
  }

  payload += "]}}";
  HomeAssistantMqttClient.publish(homeAssistantObservationTopic().c_str(), payload.c_str(), false);
}

void publishHomeAssistantDiagnostics() {
  if (!HomeAssistantMqttClient.connected()) {
    return;
  }

  char numberBuffer[24];
  RoomAggregateMetrics roomMetrics = buildRoomAggregateMetrics();

  snprintf(numberBuffer, sizeof(numberBuffer), "%lu", millis() / 1000UL);
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("uptime_s").c_str(), numberBuffer, true);

  snprintf(numberBuffer, sizeof(numberBuffer), "%u", ESP.getFreeHeap());
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("free_heap_bytes").c_str(), numberBuffer, true);

  if (WiFi.status() == WL_CONNECTED) {
    snprintf(numberBuffer, sizeof(numberBuffer), "%d", WiFi.RSSI());
    HomeAssistantMqttClient.publish(homeAssistantStateTopic("wifi_rssi_dbm").c_str(), numberBuffer, true);
    snprintf(numberBuffer, sizeof(numberBuffer), "%d", WiFi.channel());
    HomeAssistantMqttClient.publish(homeAssistantStateTopic("wifi_channel").c_str(), numberBuffer, true);
    HomeAssistantMqttClient.publish(homeAssistantStateTopic("ip_address").c_str(), WiFi.localIP().toString().c_str(), true);
  }

  String hostnameLabel = deviceHostnameLabel();
  String dashboardUrl = deviceDashboardUrl();
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("firmware_version").c_str(), firmwareVersion(), true);
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("device_hostname").c_str(), hostnameLabel.c_str(), true);
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("dashboard_url").c_str(), dashboardUrl.c_str(), true);

  snprintf(numberBuffer, sizeof(numberBuffer), "%lu", radarFramesTotal);
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("radar_frames_total").c_str(), numberBuffer, true);

  snprintf(numberBuffer, sizeof(numberBuffer), "%u", roomMetrics.peopleEstimate);
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("room_people_estimate").c_str(), numberBuffer, true);

  snprintf(numberBuffer, sizeof(numberBuffer), "%u", roomMetrics.activeNodeCount);
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("room_active_nodes").c_str(), numberBuffer, true);

  snprintf(numberBuffer, sizeof(numberBuffer), "%u", roomMetrics.peerNodeCount);
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("room_peer_nodes").c_str(), numberBuffer, true);

  snprintf(numberBuffer, sizeof(numberBuffer), "%u", roomMetrics.activityScore);
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("room_activity_score").c_str(), numberBuffer, true);

  publishObservationFeed();
  publishHomeAssistantDerivedMetrics();
  publishBleStates();
  publishRoomCollaborationSummary();
}

void publishHomeAssistantDerivedMetrics() {
  if (!HomeAssistantMqttClient.connected()) {
    return;
  }

  RadarDerivedMetrics metrics = buildRadarDerivedMetrics();
  char numberBuffer[24];

  snprintf(numberBuffer, sizeof(numberBuffer), "%u", metrics.estimatedPeople);
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("people_estimate").c_str(), numberBuffer, true);

  snprintf(numberBuffer, sizeof(numberBuffer), "%u", metrics.activeGateCount);
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("active_gate_count").c_str(), numberBuffer, true);

  snprintf(numberBuffer, sizeof(numberBuffer), "%u", metrics.activityScore);
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("activity_score").c_str(), numberBuffer, true);

  snprintf(numberBuffer, sizeof(numberBuffer), "%d", metrics.dominantGateIndex);
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("dominant_gate_index").c_str(), numberBuffer, true);

  snprintf(numberBuffer, sizeof(numberBuffer), "%d", metrics.dominantGateDistanceCm);
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("dominant_gate_distance_cm").c_str(), numberBuffer, true);

  snprintf(numberBuffer, sizeof(numberBuffer), "%u", metrics.dominantGateEnergy);
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("dominant_gate_energy").c_str(), numberBuffer, true);

  snprintf(numberBuffer, sizeof(numberBuffer), "%lu", metrics.totalGateEnergy);
  HomeAssistantMqttClient.publish(homeAssistantStateTopic("total_gate_energy").c_str(), numberBuffer, true);

  for (uint8_t gateIndex = 0; gateIndex < LD2420_GATE_COUNT; gateIndex++) {
    char objectId[20];
    snprintf(objectId, sizeof(objectId), "gate_%02u_energy", gateIndex);

    const uint16_t gateEnergy = latestEnergyFrameSnapshot.valid ? latestEnergyFrameSnapshot.gates[gateIndex] : 0;
    snprintf(numberBuffer, sizeof(numberBuffer), "%u", gateEnergy);
    HomeAssistantMqttClient.publish(homeAssistantStateTopic(objectId).c_str(), numberBuffer, true);
  }
}

void publishHomeAssistantStates() {
  publishHomeAssistantAvailability(true);
  publishHomeAssistantPresence();
  publishHomeAssistantDistance();
  publishHomeAssistantDiagnostics();
  publishBleStates();
}

void publishHomeAssistantDiscovery() {
  if (!HomeAssistantMqttClient.connected() || homeAssistantDiscoveryPublished) {
    return;
  }

  String deviceId = runtimeConfig.nodeId;
  String friendlyName = runtimeConfig.friendlyName;
  String availabilityTopic = homeAssistantDeviceTopic() + "/availability";
  String presenceTopic = homeAssistantStateTopic("presence");
  String distanceTopic = homeAssistantStateTopic("distance_cm");
  String uptimeTopic = homeAssistantStateTopic("uptime_s");
  String freeHeapTopic = homeAssistantStateTopic("free_heap_bytes");
  String wifiRssiTopic = homeAssistantStateTopic("wifi_rssi_dbm");
  String wifiChannelTopic = homeAssistantStateTopic("wifi_channel");
  String ipAddressTopic = homeAssistantStateTopic("ip_address");
  String firmwareVersionTopic = homeAssistantStateTopic("firmware_version");
  String hostnameTopic = homeAssistantStateTopic("device_hostname");
  String dashboardUrlTopic = homeAssistantStateTopic("dashboard_url");
  String radarFramesTopic = homeAssistantStateTopic("radar_frames_total");
  String peopleEstimateTopic = homeAssistantStateTopic("people_estimate");
  String activeGateCountTopic = homeAssistantStateTopic("active_gate_count");
  String activityScoreTopic = homeAssistantStateTopic("activity_score");
  String dominantGateIndexTopic = homeAssistantStateTopic("dominant_gate_index");
  String dominantGateDistanceTopic = homeAssistantStateTopic("dominant_gate_distance_cm");
  String dominantGateEnergyTopic = homeAssistantStateTopic("dominant_gate_energy");
  String totalGateEnergyTopic = homeAssistantStateTopic("total_gate_energy");
  String roomPeopleEstimateTopic = homeAssistantStateTopic("room_people_estimate");
  String roomActiveNodesTopic = homeAssistantStateTopic("room_active_nodes");
  String roomPeerNodesTopic = homeAssistantStateTopic("room_peer_nodes");
  String roomActivityScoreTopic = homeAssistantStateTopic("room_activity_score");
  String bleBeaconCountTopic = homeAssistantStateTopic("ble_beacon_count");
  String bleBeaconsJsonTopic = homeAssistantStateTopic("ble_beacons_json");
  String bleTaggedPeopleCountTopic = homeAssistantStateTopic("ble_tagged_people_count");
  String escapedFriendlyName = jsonEscape(friendlyName);
  String escapedDeviceId = jsonEscape(deviceId);

  String deviceJson = String("\"dev\":{\"ids\":[\"") + escapedDeviceId +
                      "\"],\"name\":\"" + escapedFriendlyName +
                      "\",\"mdl\":\"" + jsonEscape(String(kBoardProfile.displayName) + " + " + kBoardProfile.radarModel) +
                      "\",\"mf\":\"" + jsonEscape(kBoardProfile.manufacturer) + "\"}";

  String presencePayload = String("{") +
                           "\"name\":\"" + escapedFriendlyName + " Presence\"," +
                           "\"uniq_id\":\"" + escapedDeviceId + "_presence\"," +
                           "\"stat_t\":\"" + presenceTopic + "\"," +
                           "\"pl_on\":\"ON\"," +
                           "\"pl_off\":\"OFF\"," +
                           "\"dev_cla\":\"occupancy\"," +
                           "\"avty_t\":\"" + availabilityTopic + "\"," +
                           "\"pl_avail\":\"online\"," +
                           "\"pl_not_avail\":\"offline\"," +
                           deviceJson +
                           "}";

  String distancePayload = buildHomeAssistantSensorPayload(
                           escapedFriendlyName + " Distance",
                           escapedDeviceId + "_distance_cm",
                           distanceTopic,
                           availabilityTopic,
                           deviceJson,
                           "\"unit_of_meas\":\"cm\",\"dev_cla\":\"distance\",\"state_class\":\"measurement\",");

  String uptimePayload = buildHomeAssistantSensorPayload(
                         escapedFriendlyName + " Uptime",
                         escapedDeviceId + "_uptime_s",
                         uptimeTopic,
                         availabilityTopic,
                         deviceJson,
                         "\"unit_of_meas\":\"s\",\"dev_cla\":\"duration\",\"state_class\":\"measurement\",\"ent_cat\":\"diagnostic\",");

  String freeHeapPayload = buildHomeAssistantSensorPayload(
                           escapedFriendlyName + " Free Heap",
                           escapedDeviceId + "_free_heap_bytes",
                           freeHeapTopic,
                           availabilityTopic,
                           deviceJson,
                           "\"unit_of_meas\":\"B\",\"dev_cla\":\"data_size\",\"state_class\":\"measurement\",\"ent_cat\":\"diagnostic\",");

  String wifiRssiPayload = buildHomeAssistantSensorPayload(
                           escapedFriendlyName + " WiFi RSSI",
                           escapedDeviceId + "_wifi_rssi_dbm",
                           wifiRssiTopic,
                           availabilityTopic,
                           deviceJson,
                           "\"unit_of_meas\":\"dBm\",\"dev_cla\":\"signal_strength\",\"state_class\":\"measurement\",\"ent_cat\":\"diagnostic\",");

  String wifiChannelPayload = buildHomeAssistantSensorPayload(
                              escapedFriendlyName + " WiFi Channel",
                              escapedDeviceId + "_wifi_channel",
                              wifiChannelTopic,
                              availabilityTopic,
                              deviceJson,
                              "\"state_class\":\"measurement\",\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:wifi-cog\",");

  String ipAddressPayload = buildHomeAssistantSensorPayload(
                            escapedFriendlyName + " IP Address",
                            escapedDeviceId + "_ip_address",
                            ipAddressTopic,
                            availabilityTopic,
                            deviceJson,
                            "\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:ip-network\",");

  String firmwareVersionPayload = buildHomeAssistantSensorPayload(
                           escapedFriendlyName + " Firmware Version",
                           escapedDeviceId + "_firmware_version",
                           firmwareVersionTopic,
                           availabilityTopic,
                           deviceJson,
                           "\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:tag-text\",");

  String hostnamePayload = buildHomeAssistantSensorPayload(
                           escapedFriendlyName + " Hostname",
                           escapedDeviceId + "_device_hostname",
                           hostnameTopic,
                           availabilityTopic,
                           deviceJson,
                           "\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:identifier\",");

  String dashboardUrlPayload = buildHomeAssistantSensorPayload(
                               escapedFriendlyName + " Dashboard URL",
                               escapedDeviceId + "_dashboard_url",
                               dashboardUrlTopic,
                               availabilityTopic,
                               deviceJson,
                               "\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:web\",");

  String radarFramesPayload = buildHomeAssistantSensorPayload(
                              escapedFriendlyName + " Radar Frames",
                              escapedDeviceId + "_radar_frames_total",
                              radarFramesTopic,
                              availabilityTopic,
                              deviceJson,
                              "\"state_class\":\"total_increasing\",\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:radar\",");

  String peopleEstimatePayload = buildHomeAssistantSensorPayload(
                                escapedFriendlyName + " People Estimate",
                                escapedDeviceId + "_people_estimate",
                                peopleEstimateTopic,
                                availabilityTopic,
                                deviceJson,
                                "\"state_class\":\"measurement\",\"ic\":\"mdi:account-group\",");

  String activeGateCountPayload = buildHomeAssistantSensorPayload(
                                 escapedFriendlyName + " Active Gates",
                                 escapedDeviceId + "_active_gate_count",
                                 activeGateCountTopic,
                                 availabilityTopic,
                                 deviceJson,
                                 "\"state_class\":\"measurement\",\"ic\":\"mdi:tune-variant\",");

  String activityScorePayload = buildHomeAssistantSensorPayload(
                               escapedFriendlyName + " Activity Score",
                               escapedDeviceId + "_activity_score",
                               activityScoreTopic,
                               availabilityTopic,
                               deviceJson,
                               "\"unit_of_meas\":\"score\",\"state_class\":\"measurement\",\"ic\":\"mdi:chart-line\",");

  String dominantGateIndexPayload = buildHomeAssistantSensorPayload(
                                   escapedFriendlyName + " Dominant Gate Index",
                                   escapedDeviceId + "_dominant_gate_index",
                                   dominantGateIndexTopic,
                                   availabilityTopic,
                                   deviceJson,
                                   "\"state_class\":\"measurement\",\"ic\":\"mdi:map-marker-radius\",");

  String dominantGateDistancePayload = buildHomeAssistantSensorPayload(
                                      escapedFriendlyName + " Dominant Gate Distance",
                                      escapedDeviceId + "_dominant_gate_distance_cm",
                                      dominantGateDistanceTopic,
                                      availabilityTopic,
                                      deviceJson,
                                      "\"unit_of_meas\":\"cm\",\"dev_cla\":\"distance\",\"state_class\":\"measurement\",\"ic\":\"mdi:map-marker-distance\",");

  String dominantGateEnergyPayload = buildHomeAssistantSensorPayload(
                                    escapedFriendlyName + " Dominant Gate Energy",
                                    escapedDeviceId + "_dominant_gate_energy",
                                    dominantGateEnergyTopic,
                                    availabilityTopic,
                                    deviceJson,
                                    "\"state_class\":\"measurement\",\"ic\":\"mdi:pulse\",");

  String totalGateEnergyPayload = buildHomeAssistantSensorPayload(
                                escapedFriendlyName + " Total Gate Energy",
                                escapedDeviceId + "_total_gate_energy",
                                totalGateEnergyTopic,
                                availabilityTopic,
                                deviceJson,
                                "\"state_class\":\"measurement\",\"ic\":\"mdi:waves\",");

  String roomPeopleEstimatePayload = buildHomeAssistantSensorPayload(
                                 escapedFriendlyName + " Room People Estimate",
                                 escapedDeviceId + "_room_people_estimate",
                                 roomPeopleEstimateTopic,
                                 availabilityTopic,
                                 deviceJson,
                                 "\"state_class\":\"measurement\",\"ic\":\"mdi:account-multiple\",");

  String roomActiveNodesPayload = buildHomeAssistantSensorPayload(
                                 escapedFriendlyName + " Room Active Nodes",
                                 escapedDeviceId + "_room_active_nodes",
                                 roomActiveNodesTopic,
                                 availabilityTopic,
                                 deviceJson,
                                 "\"state_class\":\"measurement\",\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:access-point-network\",");

  String roomPeerNodesPayload = buildHomeAssistantSensorPayload(
                                 escapedFriendlyName + " Room Peer Nodes",
                                 escapedDeviceId + "_room_peer_nodes",
                                 roomPeerNodesTopic,
                                 availabilityTopic,
                                 deviceJson,
                                 "\"state_class\":\"measurement\",\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:lan-connect\",");

  String roomActivityScorePayload = buildHomeAssistantSensorPayload(
                                 escapedFriendlyName + " Room Activity Score",
                                 escapedDeviceId + "_room_activity_score",
                                 roomActivityScoreTopic,
                                 availabilityTopic,
                                 deviceJson,
                                 "\"unit_of_meas\":\"score\",\"state_class\":\"measurement\",\"ic\":\"mdi:motion-sensor\",");

  String bleBeaconCountPayload = buildHomeAssistantSensorPayload(
                                 escapedFriendlyName + " BLE Beacon Count",
                                 escapedDeviceId + "_ble_beacon_count",
                                 bleBeaconCountTopic,
                                 availabilityTopic,
                                 deviceJson,
                                 "\"state_class\":\"measurement\",\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:bluetooth-audio\",");

  String bleBeaconsJsonPayload = buildHomeAssistantSensorPayload(
                                 escapedFriendlyName + " BLE Sightings",
                                 escapedDeviceId + "_ble_beacons_json",
                                 bleBeaconsJsonTopic,
                                 availabilityTopic,
                                 deviceJson,
                                 "\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:bluetooth-transfer\",");

  String bleTaggedPeopleCountPayload = buildHomeAssistantSensorPayload(
                                 escapedFriendlyName + " BLE Tagged People",
                                 escapedDeviceId + "_ble_tagged_people_count",
                                 bleTaggedPeopleCountTopic,
                                 availabilityTopic,
                                 deviceJson,
                                 "\"state_class\":\"measurement\",\"ic\":\"mdi:account-box-multiple\",");

  String presenceDiscoveryTopic = homeAssistantDiscoveryTopic("binary_sensor", "presence");
  String distanceDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "distance_cm");
  String uptimeDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "uptime_s");
  String freeHeapDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "free_heap_bytes");
  String wifiRssiDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "wifi_rssi_dbm");
  String wifiChannelDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "wifi_channel");
  String ipAddressDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "ip_address");
  String firmwareVersionDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "firmware_version");
  String hostnameDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "device_hostname");
  String dashboardUrlDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "dashboard_url");
  String radarFramesDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "radar_frames_total");
  String peopleEstimateDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "people_estimate");
  String activeGateCountDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "active_gate_count");
  String activityScoreDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "activity_score");
  String dominantGateIndexDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "dominant_gate_index");
  String dominantGateDistanceDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "dominant_gate_distance_cm");
  String dominantGateEnergyDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "dominant_gate_energy");
  String totalGateEnergyDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "total_gate_energy");
  String roomPeopleEstimateDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "room_people_estimate");
  String roomActiveNodesDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "room_active_nodes");
  String roomPeerNodesDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "room_peer_nodes");
  String roomActivityScoreDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "room_activity_score");
  String bleBeaconCountDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "ble_beacon_count");
  String bleBeaconsJsonDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "ble_beacons_json");
  String bleTaggedPeopleCountDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", "ble_tagged_people_count");

  bool published = HomeAssistantMqttClient.publish(presenceDiscoveryTopic.c_str(), presencePayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(distanceDiscoveryTopic.c_str(), distancePayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(uptimeDiscoveryTopic.c_str(), uptimePayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(freeHeapDiscoveryTopic.c_str(), freeHeapPayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(wifiRssiDiscoveryTopic.c_str(), wifiRssiPayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(wifiChannelDiscoveryTopic.c_str(), wifiChannelPayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(ipAddressDiscoveryTopic.c_str(), ipAddressPayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(firmwareVersionDiscoveryTopic.c_str(), firmwareVersionPayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(hostnameDiscoveryTopic.c_str(), hostnamePayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(dashboardUrlDiscoveryTopic.c_str(), dashboardUrlPayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(radarFramesDiscoveryTopic.c_str(), radarFramesPayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(peopleEstimateDiscoveryTopic.c_str(), peopleEstimatePayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(activeGateCountDiscoveryTopic.c_str(), activeGateCountPayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(activityScoreDiscoveryTopic.c_str(), activityScorePayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(dominantGateIndexDiscoveryTopic.c_str(), dominantGateIndexPayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(dominantGateDistanceDiscoveryTopic.c_str(), dominantGateDistancePayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(dominantGateEnergyDiscoveryTopic.c_str(), dominantGateEnergyPayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(totalGateEnergyDiscoveryTopic.c_str(), totalGateEnergyPayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(roomPeopleEstimateDiscoveryTopic.c_str(), roomPeopleEstimatePayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(roomActiveNodesDiscoveryTopic.c_str(), roomActiveNodesPayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(roomPeerNodesDiscoveryTopic.c_str(), roomPeerNodesPayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(roomActivityScoreDiscoveryTopic.c_str(), roomActivityScorePayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(bleBeaconCountDiscoveryTopic.c_str(), bleBeaconCountPayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(bleBeaconsJsonDiscoveryTopic.c_str(), bleBeaconsJsonPayload.c_str(), true) &&
                   HomeAssistantMqttClient.publish(bleTaggedPeopleCountDiscoveryTopic.c_str(), bleTaggedPeopleCountPayload.c_str(), true);

  for (uint8_t tagIndex = 0; published && tagIndex < MAX_BLE_TAGS; tagIndex++) {
    char trackerObjectId[24];
    char rssiObjectId[24];
    char trackerUniqueId[48];
    char rssiUniqueId[48];
    snprintf(trackerObjectId, sizeof(trackerObjectId), "ble_tag_%02u_tracker", tagIndex);
    snprintf(rssiObjectId, sizeof(rssiObjectId), "ble_tag_%02u_rssi", tagIndex);
    snprintf(trackerUniqueId, sizeof(trackerUniqueId), "%s_ble_tag_%02u_tracker", escapedDeviceId.c_str(), tagIndex);
    snprintf(rssiUniqueId, sizeof(rssiUniqueId), "%s_ble_tag_%02u_rssi", escapedDeviceId.c_str(), tagIndex);

    String trackerDiscoveryTopic = homeAssistantDiscoveryTopic("device_tracker", trackerObjectId);
    String rssiDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", rssiObjectId);

    if (!bleIdentityTags[tagIndex].occupied) {
      published = HomeAssistantMqttClient.publish(trackerDiscoveryTopic.c_str(), "", true) &&
                  HomeAssistantMqttClient.publish(rssiDiscoveryTopic.c_str(), "", true);
      continue;
    }

    String escapedTagLabel = jsonEscape(bleIdentityTags[tagIndex].label);
    String trackerPayload = String("{") +
                            "\"name\":\"" + escapedTagLabel + "\"," +
                            "\"uniq_id\":\"" + trackerUniqueId + "\"," +
                            "\"stat_t\":\"" + homeAssistantStateTopic(trackerObjectId) + "\"," +
                            "\"pl_home\":\"home\"," +
                            "\"pl_not_home\":\"not_home\"," +
                            "\"src_type\":\"bluetooth_le\"," +
                            "\"avty_t\":\"" + availabilityTopic + "\"," +
                            "\"pl_avail\":\"online\"," +
                            "\"pl_not_avail\":\"offline\"," +
                            deviceJson +
                            "}";

    String rssiPayload = buildHomeAssistantSensorPayload(
                         escapedTagLabel + " RSSI",
                         rssiUniqueId,
                         homeAssistantStateTopic(rssiObjectId),
                         availabilityTopic,
                         deviceJson,
                         "\"unit_of_meas\":\"dBm\",\"dev_cla\":\"signal_strength\",\"state_class\":\"measurement\",\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:bluetooth\",");

    published = HomeAssistantMqttClient.publish(trackerDiscoveryTopic.c_str(), trackerPayload.c_str(), true) &&
                HomeAssistantMqttClient.publish(rssiDiscoveryTopic.c_str(), rssiPayload.c_str(), true);
  }

  for (uint8_t gateIndex = 0; published && gateIndex < LD2420_GATE_COUNT; gateIndex++) {
    char objectId[20];
    char uniqueId[40];
    snprintf(objectId, sizeof(objectId), "gate_%02u_energy", gateIndex);
    snprintf(uniqueId, sizeof(uniqueId), "%s_gate_%02u_energy", escapedDeviceId.c_str(), gateIndex);

    String gatePayload = buildHomeAssistantSensorPayload(
                         escapedFriendlyName + " Gate " + String(gateIndex) + " Energy",
                         uniqueId,
                         homeAssistantStateTopic(objectId),
                         availabilityTopic,
                         deviceJson,
                         "\"state_class\":\"measurement\",\"ent_cat\":\"diagnostic\",\"ic\":\"mdi:chart-bar\",");
    String gateDiscoveryTopic = homeAssistantDiscoveryTopic("sensor", objectId);
    published = HomeAssistantMqttClient.publish(gateDiscoveryTopic.c_str(), gatePayload.c_str(), true);
  }

  if (published) {
    homeAssistantDiscoveryPublished = true;
    publishHomeAssistantStates();
  }
}

void ensureWiFiConnected() {
  if (!homeAssistantConfigured() || WiFi.status() == WL_CONNECTED) {
    return;
  }

  uint32_t now = millis();
  if ((now - lastWiFiConnectAttemptMs) < WIFI_RETRY_MS) {
    return;
  }

  lastWiFiConnectAttemptMs = now;
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(deviceHostname().c_str());
  WiFi.begin(runtimeConfig.wifiSsid.c_str(), runtimeConfig.wifiPassword.c_str());
}

const char* firmwareVersion() {
  return ESPWAVERIDER_FIRMWARE_VERSION;
}

const char* firmwareBuildTarget() {
  return ESPWAVERIDER_BUILD_TARGET;
}

const char* firmwareGitSha() {
  return ESPWAVERIDER_GIT_SHA;
}

String semanticVersionCore(const String& version) {
  String trimmed = version;
  trimmed.trim();
  if (trimmed.length() == 0) {
    return "";
  }

  int cursor = (trimmed[0] == 'v' || trimmed[0] == 'V') ? 1 : 0;
  const int majorStart = cursor;
  while (cursor < trimmed.length() && isDigit(static_cast<unsigned char>(trimmed[cursor]))) {
    cursor++;
  }
  if (cursor <= majorStart || cursor >= trimmed.length() || trimmed[cursor] != '.') {
    return "";
  }

  const int minorStart = ++cursor;
  while (cursor < trimmed.length() && isDigit(static_cast<unsigned char>(trimmed[cursor]))) {
    cursor++;
  }
  if (cursor <= minorStart || cursor >= trimmed.length() || trimmed[cursor] != '.') {
    return "";
  }

  const int patchStart = ++cursor;
  while (cursor < trimmed.length() && isDigit(static_cast<unsigned char>(trimmed[cursor]))) {
    cursor++;
  }
  if (cursor <= patchStart) {
    return "";
  }

  return trimmed.substring(majorStart, cursor);
}

int compareSemanticVersions(const String& leftVersion, const String& rightVersion) {
  const String leftCore = semanticVersionCore(leftVersion);
  const String rightCore = semanticVersionCore(rightVersion);
  if (leftCore.length() == 0 && rightCore.length() == 0) {
    return 0;
  }
  if (leftCore.length() == 0) {
    return -1;
  }
  if (rightCore.length() == 0) {
    return 1;
  }

  int leftParts[3] = {0, 0, 0};
  int rightParts[3] = {0, 0, 0};
  int partIndex = 0;
  int start = 0;
  for (int i = 0; i <= leftCore.length() && partIndex < 3; i++) {
    if (i == leftCore.length() || leftCore[i] == '.') {
      leftParts[partIndex++] = leftCore.substring(start, i).toInt();
      start = i + 1;
    }
  }
  partIndex = 0;
  start = 0;
  for (int i = 0; i <= rightCore.length() && partIndex < 3; i++) {
    if (i == rightCore.length() || rightCore[i] == '.') {
      rightParts[partIndex++] = rightCore.substring(start, i).toInt();
      start = i + 1;
    }
  }

  for (int i = 0; i < 3; i++) {
    if (leftParts[i] < rightParts[i]) {
      return -1;
    }
    if (leftParts[i] > rightParts[i]) {
      return 1;
    }
  }

  return 0;
}

bool findHighestPeerReleaseVersion(String& nodeId, String& version, String& source) {
  String bestNodeId;
  String bestVersion;
  String bestSource;
  const String localBuildTarget = firmwareBuildTarget();

  const uint32_t now = millis();
  auto considerPeer = [&](const String& candidateNodeId, const String& candidateVersion, const String& candidateBuildTarget, const String& candidateSource) {
    const String candidateCore = semanticVersionCore(candidateVersion);
    if (candidateNodeId.length() == 0 || candidateCore.length() == 0 || candidateBuildTarget != localBuildTarget) {
      return;
    }

    if (bestVersion.length() == 0 || compareSemanticVersions(candidateCore, bestVersion) > 0) {
      bestNodeId = candidateNodeId;
      bestVersion = candidateCore;
      bestSource = candidateSource;
    }
  };

  for (uint8_t index = 0; index < MAX_ROOM_PEERS; index++) {
    const RoomPeerSummary& peer = roomPeers[index];
    if (!peer.occupied || (now - peer.lastUpdatedMs) > ROOM_PEER_FRESHNESS_MS) {
      continue;
    }
    considerPeer(peer.nodeId, peer.firmwareVersion, peer.buildTarget, "room_summary");
  }

  for (uint8_t index = 0; index < MAX_UDP_DISCOVERY_PEERS; index++) {
    const UdpDiscoveryPeer& peer = udpDiscoveryPeers[index];
    if (!peer.occupied || (now - peer.lastSeenMs) > UDP_DISCOVERY_PEER_FRESHNESS_MS) {
      continue;
    }
    considerPeer(peer.nodeId, peer.firmwareVersion, peer.buildTarget, "udp_discovery");
  }

  nodeId = bestNodeId;
  version = bestVersion;
  source = bestSource;
  return bestVersion.length() > 0;
}

String firmwareReleaseAssetUrl(const String& versionCore) {
  const String normalizedCore = semanticVersionCore(versionCore);
  if (normalizedCore.length() == 0) {
    return "";
  }

  const String tag = "v" + normalizedCore;
  const String assetName = firmwareReleaseAssetName(normalizedCore, ".bin");
  return String("https://github.com/") + FIRMWARE_RELEASE_REPO_OWNER + "/" + FIRMWARE_RELEASE_REPO_NAME + "/releases/download/" + tag + "/" + assetName;
}

String firmwareReleaseAssetName(const String& versionCore, const char* extension) {
  const String normalizedCore = semanticVersionCore(versionCore);
  if (normalizedCore.length() == 0) {
    return "";
  }

  return String("EspWaveRider-") + normalizedCore + "-" + firmwareBuildTarget() + extension;
}

String firmwareReleaseApiUrl(const String& versionCore) {
  const String normalizedCore = semanticVersionCore(versionCore);
  if (normalizedCore.length() == 0) {
    return "";
  }

  return String("https://api.github.com/repos/") + FIRMWARE_RELEASE_REPO_OWNER + "/" + FIRMWARE_RELEASE_REPO_NAME + "/releases/tags/v" + normalizedCore;
}

bool beginTrustedFirmwareRequest(HTTPClient& http, WiFiClientSecure& client, const String& url, String& error) {
  client.setCACert(FIRMWARE_RELEASE_TRUST_ANCHORS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(20000);

  if (http.begin(client, url)) {
    return true;
  }

  error = "unable_to_open_trusted_https_request";
  return false;
}

bool ensureTrustedTlsClock(String& error) {
  time_t now = time(nullptr);
  if (now >= MIN_TRUSTED_TLS_UNIX_TIME) {
    return true;
  }

  configTime(0, 0, "time.cloudflare.com", "pool.ntp.org", "time.google.com");
  const uint32_t startMs = millis();
  while ((millis() - startMs) < FIRMWARE_TLS_TIME_SYNC_WAIT_MS) {
    now = time(nullptr);
    if (now >= MIN_TRUSTED_TLS_UNIX_TIME) {
      return true;
    }
    delay(200);
  }

  error = "clock_not_synced_for_tls";
  return false;
}

String extractSha256Hex(const String& value) {
  String digest;
  digest.reserve(64);

  for (size_t index = 0; index < value.length(); index++) {
    const char current = value[index];
    const bool isDigit = current >= '0' && current <= '9';
    const bool isLowerHex = current >= 'a' && current <= 'f';
    const bool isUpperHex = current >= 'A' && current <= 'F';

    if (isDigit || isLowerHex || isUpperHex) {
      digest += static_cast<char>(tolower(current));
      if (digest.length() == 64) {
        return digest;
      }
      continue;
    }

    if (digest.length() > 0) {
      digest = "";
    }
  }

  return "";
}

String extractGitHubApiMessage(const String& value) {
  const int messageFieldIndex = value.indexOf("\"message\":\"");
  if (messageFieldIndex < 0) {
    return "";
  }

  const int messageStart = messageFieldIndex + strlen("\"message\":\"");
  const int messageEnd = value.indexOf('"', messageStart);
  if (messageEnd <= messageStart) {
    return "";
  }

  String message = value.substring(messageStart, messageEnd);
  message.replace(" ", "_");
  message.replace("\n", "_");
  message.replace("\r", "_");
  return message;
}

String fetchFirmwareReleaseChecksum(const String& versionCore, String& error) {
  const String releaseApiUrl = firmwareReleaseApiUrl(versionCore);
  const String firmwareAssetName = firmwareReleaseAssetName(versionCore, ".bin");
  if (releaseApiUrl.length() == 0 || firmwareAssetName.length() == 0) {
    error = "release_api_url_missing";
    return "";
  }

  WiFiClientSecure client;
  HTTPClient http;
  if (!beginTrustedFirmwareRequest(http, client, releaseApiUrl, error)) {
    return "";
  }

  http.setUserAgent("EspWaveRider-OTA/1.0");
  http.addHeader("User-Agent", "EspWaveRider-OTA/1.0");
  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("X-GitHub-Api-Version", "2022-11-28");

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    error = String("release_api_http_") + httpCode;
    if (httpCode < 0) {
      error += ":" + HTTPClient::errorToString(httpCode);
    } else {
      const String responseBody = http.getString();
      const String apiMessage = extractGitHubApiMessage(responseBody);
      if (apiMessage.length() > 0) {
        error += ":" + apiMessage;
      }
    }
    http.end();
    return "";
  }

  const String releaseBody = http.getString();
  http.end();

  const int assetNameIndex = releaseBody.indexOf(String("\"name\":\"") + firmwareAssetName + "\"");
  if (assetNameIndex < 0) {
    error = "release_api_asset_missing";
    return "";
  }

  const int digestFieldIndex = releaseBody.indexOf("\"digest\":\"", assetNameIndex);
  if (digestFieldIndex < 0) {
    error = "release_api_digest_missing";
    return "";
  }

  const int digestValueStart = digestFieldIndex + strlen("\"digest\":\"");
  const int digestValueEnd = releaseBody.indexOf('"', digestValueStart);
  if (digestValueEnd <= digestValueStart) {
    error = "release_api_digest_parse_failed";
    return "";
  }

  const String checksum = extractSha256Hex(releaseBody.substring(digestValueStart, digestValueEnd));
  if (checksum.length() == 64) {
    return checksum;
  }

  error = "release_api_digest_invalid";
  return "";
}

bool downloadAndApplyFirmware(const String& downloadUrl, const String& expectedSha256, String& error) {
  WiFiClientSecure client;
  HTTPClient http;
  if (!beginTrustedFirmwareRequest(http, client, downloadUrl, error)) {
    return false;
  }

  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    error = String("download_http_") + httpCode;
    if (httpCode < 0) {
      error += ":" + HTTPClient::errorToString(httpCode);
    }
    http.end();
    return false;
  }

  const int contentLength = http.getSize();
  if (contentLength <= 0) {
    error = "download_size_missing";
    http.end();
    return false;
  }

  if (!Update.begin(contentLength, U_FLASH)) {
    error = String("update_begin_failed:") + Update.errorString();
    http.end();
    return false;
  }

  mbedtls_sha256_context sha256Context;
  mbedtls_sha256_init(&sha256Context);
  mbedtls_sha256_starts_ret(&sha256Context, 0);

  WiFiClient* stream = http.getStreamPtr();
  static uint8_t buffer[FIRMWARE_DOWNLOAD_BUFFER_SIZE];
  size_t bytesWritten = 0;
  uint32_t lastChunkMs = millis();

  while (http.connected() && bytesWritten < static_cast<size_t>(contentLength)) {
    const size_t available = stream->available();
    if (available == 0) {
      if ((millis() - lastChunkMs) > 15000) {
        error = "download_timeout";
        Update.abort();
        http.end();
        mbedtls_sha256_free(&sha256Context);
        return false;
      }
      delay(1);
      continue;
    }

    const size_t requested = min(sizeof(buffer), min(available, static_cast<size_t>(contentLength) - bytesWritten));
    const size_t chunkSize = stream->readBytes(buffer, requested);
    if (chunkSize == 0) {
      continue;
    }

    lastChunkMs = millis();
    mbedtls_sha256_update_ret(&sha256Context, buffer, chunkSize);

    if (Update.write(buffer, chunkSize) != chunkSize) {
      error = String("update_write_failed:") + Update.errorString();
      Update.abort();
      http.end();
      mbedtls_sha256_free(&sha256Context);
      return false;
    }

    bytesWritten += chunkSize;
  }

  http.end();

  if (bytesWritten != static_cast<size_t>(contentLength)) {
    error = "download_incomplete";
    Update.abort();
    mbedtls_sha256_free(&sha256Context);
    return false;
  }

  uint8_t actualDigestBytes[32];
  mbedtls_sha256_finish_ret(&sha256Context, actualDigestBytes);
  mbedtls_sha256_free(&sha256Context);

  static constexpr char HEX_DIGITS[] = "0123456789abcdef";
  String actualDigest;
  actualDigest.reserve(64);
  for (size_t index = 0; index < sizeof(actualDigestBytes); index++) {
    const uint8_t value = actualDigestBytes[index];
    actualDigest += HEX_DIGITS[(value >> 4) & 0x0F];
    actualDigest += HEX_DIGITS[value & 0x0F];
  }

  if (actualDigest != expectedSha256) {
    error = String("sha256_mismatch:") + actualDigest;
    Update.abort();
    return false;
  }

  if (!Update.end()) {
    error = String("update_finalize_failed:") + Update.errorString();
    Update.abort();
    return false;
  }

  if (!Update.isFinished()) {
    error = "update_not_finished";
    Update.abort();
    return false;
  }

  return true;
}

void appendFirmwareSyncJson(String& json) {
  String highestPeerNodeId;
  String highestPeerVersion;
  String highestPeerSource;
  const bool hasPeerRelease = findHighestPeerReleaseVersion(highestPeerNodeId, highestPeerVersion, highestPeerSource);
  const String localCore = semanticVersionCore(firmwareVersion());
  const bool syncAvailable = hasPeerRelease && compareSemanticVersions(highestPeerVersion, localCore) > 0;

  json += ",\"firmware_sync\":{";
  json += "\"local_version_core\":\"" + jsonEscape(localCore) + "\"";
  json += ",\"highest_peer_node_id\":\"" + jsonEscape(highestPeerNodeId) + "\"";
  json += ",\"highest_peer_version\":\"" + jsonEscape(highestPeerVersion) + "\"";
  json += ",\"highest_peer_source\":\"" + jsonEscape(highestPeerSource) + "\"";
  json += ",\"sync_available\":";
  json += syncAvailable ? "true" : "false";
  json += ",\"in_progress\":";
  json += firmwareSyncState.inProgress ? "true" : "false";
  json += ",\"pending\":";
  json += firmwareSyncState.pending ? "true" : "false";
  json += ",\"target_version\":\"" + jsonEscape(firmwareSyncState.targetVersion) + "\"";
  json += ",\"target_node_id\":\"" + jsonEscape(firmwareSyncState.targetNodeId) + "\"";
  json += ",\"target_source\":\"" + jsonEscape(firmwareSyncState.targetSource) + "\"";
  json += ",\"download_url\":\"" + jsonEscape(firmwareSyncState.downloadUrl) + "\"";
  json += ",\"status\":\"" + jsonEscape(firmwareSyncState.statusText) + "\"";
  json += ",\"last_error\":\"" + jsonEscape(firmwareSyncState.lastError) + "\"";
  json += ",\"last_started_ms\":" + String(firmwareSyncState.lastStartedMs);
  json += ",\"last_completed_ms\":" + String(firmwareSyncState.lastCompletedMs);
  json += ",\"last_success\":";
  json += firmwareSyncState.lastSuccess ? "true" : "false";
  json += "}";
}

void requestFirmwareUpdate(const String& targetVersion, const String& targetNodeId, const String& targetSource) {
  const String targetCore = semanticVersionCore(targetVersion);
  if (targetCore.length() == 0) {
    firmwareSyncState.lastSuccess = false;
    firmwareSyncState.lastError = "target_version_is_not_a_release";
    firmwareSyncState.statusText = "Peer version is not a release build; GitHub sync is unavailable.";
    firmwareSyncState.lastCompletedMs = millis();
    emitErrorEvent("firmware_update_invalid_target_version");
    return;
  }

  if (firmwareSyncState.pending || firmwareSyncState.inProgress) {
    firmwareSyncState.lastSuccess = false;
    firmwareSyncState.lastError = "firmware_sync_busy";
    firmwareSyncState.statusText = "Firmware sync already in progress.";
    firmwareSyncState.lastCompletedMs = millis();
    emitErrorEvent("firmware_sync_busy");
    return;
  }

  firmwareSyncState.pending = true;
  firmwareSyncState.lastSuccess = false;
  firmwareSyncState.targetVersion = targetCore;
  firmwareSyncState.targetNodeId = targetNodeId;
  firmwareSyncState.targetSource = targetSource;
  firmwareSyncState.downloadUrl = firmwareReleaseAssetUrl(targetCore);
  firmwareSyncState.lastError = "";
  firmwareSyncState.statusText = String("Queued firmware sync to ") + targetCore;

  printJsonEventPrefix("firmware_sync_queued");
  Serial.print(",\"target_version\":");
  printJsonString(targetCore);
  Serial.print(",\"target_node_id\":");
  printJsonString(targetNodeId);
  Serial.print(",\"target_source\":");
  printJsonString(targetSource);
  finishJsonEvent();
}

void serviceFirmwareSync() {
  if (!firmwareSyncState.pending || firmwareSyncState.inProgress) {
    return;
  }

  firmwareSyncState.pending = false;
  firmwareSyncState.inProgress = true;
  firmwareSyncState.lastStartedMs = millis();
  firmwareSyncState.lastError = "";
  firmwareSyncState.statusText = String("Downloading firmware ") + firmwareSyncState.targetVersion + " from GitHub...";

  if (WiFi.status() != WL_CONNECTED) {
    firmwareSyncState.inProgress = false;
    firmwareSyncState.lastSuccess = false;
    firmwareSyncState.lastError = "wifi_not_connected";
    firmwareSyncState.statusText = "Firmware sync failed: Wi-Fi is not connected.";
    firmwareSyncState.lastCompletedMs = millis();
    emitErrorEvent("firmware_sync_wifi_not_connected");
    return;
  }

  if (firmwareSyncState.downloadUrl.length() == 0) {
    firmwareSyncState.inProgress = false;
    firmwareSyncState.lastSuccess = false;
    firmwareSyncState.lastError = "download_url_missing";
    firmwareSyncState.statusText = "Firmware sync failed: no release asset URL was available.";
    firmwareSyncState.lastCompletedMs = millis();
    emitErrorEvent("firmware_sync_missing_download_url");
    return;
  }

  printJsonEventPrefix("firmware_sync_started");
  Serial.print(",\"target_version\":");
  printJsonString(firmwareSyncState.targetVersion);
  Serial.print(",\"download_url\":");
  printJsonString(firmwareSyncState.downloadUrl);
  finishJsonEvent();
  broadcastDeviceSnapshot();

  String tlsClockError;
  if (!ensureTrustedTlsClock(tlsClockError)) {
    firmwareSyncState.inProgress = false;
    firmwareSyncState.lastSuccess = false;
    firmwareSyncState.lastError = tlsClockError;
    firmwareSyncState.statusText = "Firmware sync failed: device clock is not trusted for TLS validation.";
    firmwareSyncState.lastCompletedMs = millis();

    printJsonEventPrefix("firmware_sync_failed");
    Serial.print(",\"target_version\":");
    printJsonString(firmwareSyncState.targetVersion);
    Serial.print(",\"error\":");
    printJsonString(firmwareSyncState.lastError);
    finishJsonEvent();
    broadcastDeviceSnapshot();
    return;
  }

  String checksumError;
  const String expectedSha256 = fetchFirmwareReleaseChecksum(firmwareSyncState.targetVersion, checksumError);
  if (expectedSha256.length() != 64) {
    firmwareSyncState.inProgress = false;
    firmwareSyncState.lastSuccess = false;
    firmwareSyncState.lastError = checksumError;
    firmwareSyncState.statusText = String("Firmware sync failed: unable to verify release checksum (") + checksumError + ").";
    firmwareSyncState.lastCompletedMs = millis();

    printJsonEventPrefix("firmware_sync_failed");
    Serial.print(",\"target_version\":");
    printJsonString(firmwareSyncState.targetVersion);
    Serial.print(",\"error\":");
    printJsonString(firmwareSyncState.lastError);
    finishJsonEvent();
    broadcastDeviceSnapshot();
    return;
  }

  firmwareSyncState.statusText = String("Verified release checksum ") + expectedSha256.substring(0, 12) + "..., downloading firmware.";
  broadcastDeviceSnapshot();

  String applyError;
  if (downloadAndApplyFirmware(firmwareSyncState.downloadUrl, expectedSha256, applyError)) {
    firmwareSyncState.inProgress = false;
    firmwareSyncState.lastSuccess = true;
    firmwareSyncState.statusText = String("Firmware ") + firmwareSyncState.targetVersion + " applied; rebooting.";
    firmwareSyncState.lastCompletedMs = millis();

    printJsonEventPrefix("firmware_sync_applied");
    Serial.print(",\"target_version\":");
    printJsonString(firmwareSyncState.targetVersion);
    finishJsonEvent();
    broadcastDeviceSnapshot();
    delay(250);
    ESP.restart();
    return;
  }

  firmwareSyncState.inProgress = false;
  firmwareSyncState.lastSuccess = false;
  firmwareSyncState.lastError = applyError;
  firmwareSyncState.statusText = String("Firmware sync failed: ") + firmwareSyncState.lastError;
  firmwareSyncState.lastCompletedMs = millis();

  printJsonEventPrefix("firmware_sync_failed");
  Serial.print(",\"target_version\":");
  printJsonString(firmwareSyncState.targetVersion);
  Serial.print(",\"error\":");
  printJsonString(firmwareSyncState.lastError);
  finishJsonEvent();
  broadcastDeviceSnapshot();
}

bool peerVersionMismatch(const String& peerVersion) {
  return peerVersion.length() == 0 || peerVersion != firmwareVersion();
}

void emitPeerVersionEvent(const char* source, const String& nodeId, const String& peerVersion, bool mismatch) {
  printJsonEventPrefix(mismatch ? "peer_version_mismatch" : "peer_version_match");
  Serial.print(",\"source\":");
  printJsonString(source != nullptr ? source : "unknown");
  Serial.print(",\"node_id\":");
  printJsonString(nodeId);
  Serial.print(",\"local_version\":");
  printJsonString(firmwareVersion());
  Serial.print(",\"peer_version\":");
  printJsonString(peerVersion.length() > 0 ? peerVersion : String("unknown"));
  Serial.print(",\"mismatch\":");
  Serial.print(mismatch ? "true" : "false");
  finishJsonEvent();
}

void ensureMqttConnected() {
  serviceMqttTransport();

  if (!homeAssistantConfigured() || WiFi.status() != WL_CONNECTED) {
    lastMqttState = -1;
    lastMqttStateText = describeMqttState(lastMqttState);
    return;
  }

  if (!HomeAssistantMqttClient.connected()) {
    uint32_t now = millis();
    if ((now - lastMqttConnectAttemptMs) < MQTT_RETRY_MS) {
      return;
    }

    lastMqttConnectAttemptMs = now;
    bool useWebSockets = runtimeConfig.mqttUseWebSockets || mqttEndpointUsesWebSockets(runtimeConfig.mqttHost);
    bool useSecureWebSockets = mqttEndpointUsesSecureWebSockets(runtimeConfig.mqttHost);
    String mqttHost = runtimeConfig.mqttHost;
    uint16_t mqttPort = runtimeConfig.mqttPort;
    String webSocketPath = normalizeWebSocketPath(runtimeConfig.mqttWebSocketPath);

    if (mqttEndpointUsesWebSockets(runtimeConfig.mqttHost)) {
      String endpoint = runtimeConfig.mqttHost;
      endpoint.trim();
      endpoint = stripUrlScheme(endpoint, "ws://");
      endpoint = stripUrlScheme(endpoint, "wss://");
      endpoint = stripUrlScheme(endpoint, "http://");
      endpoint = stripUrlScheme(endpoint, "https://");

      int slashIndex = endpoint.indexOf('/');
      String hostPort = slashIndex >= 0 ? endpoint.substring(0, slashIndex) : endpoint;
      if (slashIndex >= 0) {
        webSocketPath = normalizeWebSocketPath(endpoint.substring(slashIndex));
      }

      int colonIndex = hostPort.lastIndexOf(':');
      if (colonIndex > 0) {
        mqttHost = hostPort.substring(0, colonIndex);
        int parsedPort = hostPort.substring(colonIndex + 1).toInt();
        mqttPort = parsedPort > 0 ? static_cast<uint16_t>(parsedPort) : (useSecureWebSockets ? 443 : 80);
      } else {
        mqttHost = hostPort;
        mqttPort = runtimeConfig.mqttPort > 0 ? runtimeConfig.mqttPort : static_cast<uint16_t>(useSecureWebSockets ? 443 : 80);
      }
    }

    if (useSecureWebSockets) {
      lastResolvedMqttHost = "wss_unsupported";
      lastMqttState = -2;
      lastMqttStateText = "WSS_UNSUPPORTED";
      return;
    }

    IPAddress brokerIp;
    if (!WiFi.hostByName(mqttHost.c_str(), brokerIp)) {
      lastResolvedMqttHost = "resolve_failed";
      lastMqttState = -2;
      lastMqttStateText = "RESOLVE_FAILED";
      return;
    }

    lastResolvedMqttHost = brokerIp.toString();
    String hostHeader = runtimeConfig.mqttHostHeader;
    if (useWebSockets && hostHeader.length() == 0 && isIpv4AddressLiteral(mqttHost)) {
      hostHeader = String(HomeAssistantConfig::kMqttHostHeader);
    }

    configureMqttTransport(useWebSockets, mqttHost, mqttPort, webSocketPath, hostHeader);
    if (!useWebSockets) {
      HomeAssistantMqttClient.setServer(brokerIp, mqttPort);
    }

    String availabilityTopic = homeAssistantDeviceTopic() + "/availability";
    bool connected = false;

    if (runtimeConfig.mqttUsername.length() > 0) {
      connected = HomeAssistantMqttClient.connect(
        runtimeConfig.nodeId.c_str(),
        runtimeConfig.mqttUsername.c_str(),
        runtimeConfig.mqttPassword.c_str(),
        availabilityTopic.c_str(),
        0,
        true,
        "offline"
      );
    } else {
      connected = HomeAssistantMqttClient.connect(
        runtimeConfig.nodeId.c_str(),
        availabilityTopic.c_str(),
        0,
        true,
        "offline"
      );
    }

    if (connected) {
      subscribeRoomTopics();
      lastMqttState = 0;
      lastMqttStateText = describeMqttState(lastMqttState);
      homeAssistantDiscoveryPublished = false;
      publishHomeAssistantDiscovery();
    } else {
      lastMqttState = HomeAssistantMqttClient.state();
      lastMqttStateText = describeMqttState(lastMqttState);
    }

    return;
  }

  lastMqttState = 0;
  lastMqttStateText = describeMqttState(lastMqttState);
  HomeAssistantMqttClient.loop();
  publishHomeAssistantDiscovery();
}

void updateDistanceCm(int distanceCm) {
  if (distanceCm < 0 || lastDistanceCm == distanceCm) {
    return;
  }

  lastDistanceCm = distanceCm;
  publishHomeAssistantDistance();
}

// -----------------------------------------------------
// JSON helpers
// -----------------------------------------------------

void printJsonString(const char* value) {
  Serial.print('"');

  while (*value) {
    char c = *value++;

    switch (c) {
      case '"': Serial.print("\\\""); break;
      case '\\': Serial.print("\\\\"); break;
      case '\b': Serial.print("\\b"); break;
      case '\f': Serial.print("\\f"); break;
      case '\n': Serial.print("\\n"); break;
      case '\r': Serial.print("\\r"); break;
      case '\t': Serial.print("\\t"); break;
      default:
        if ((uint8_t)c < 0x20) {
          Serial.print("\\u00");
          if ((uint8_t)c < 0x10) {
            Serial.print('0');
          }
          Serial.print((uint8_t)c, HEX);
        } else {
          Serial.print(c);
        }
        break;
    }
  }

  Serial.print('"');
}

void printJsonString(const String& value) {
  printJsonString(value.c_str());
}

void printHexByte(uint8_t value) {
  static const char* hex = "0123456789ABCDEF";
  Serial.print(hex[(value >> 4) & 0x0F]);
  Serial.print(hex[value & 0x0F]);
}

void printJsonEventPrefix(const char* eventType) {
  Serial.print("{\"event\":");
  printJsonString(eventType);
  Serial.print(",\"ms\":");
  Serial.print(millis());
  Serial.print(",\"uptime_ms\":");
  Serial.print(millis() - bootMillis);
}

void finishJsonEvent() {
  Serial.println("}");
}

// -----------------------------------------------------
// Telemetry events
// -----------------------------------------------------

void emitBootEvent() {
  printJsonEventPrefix("boot");

  Serial.print(",\"chip_model\":");
  printJsonString(ESP.getChipModel());

  Serial.print(",\"chip_revision\":");
  Serial.print(ESP.getChipRevision());

  Serial.print(",\"cpu_mhz\":");
  Serial.print(ESP.getCpuFreqMHz());

  Serial.print(",\"flash_bytes\":");
  Serial.print(ESP.getFlashChipSize());

  Serial.print(",\"free_heap\":");
  Serial.print(ESP.getFreeHeap());

  Serial.print(",\"usb_baud\":");
  Serial.print(USB_BAUD);

  Serial.print(",\"radar_baud\":");
  Serial.print(RADAR_BAUD);

  Serial.print(",\"board_profile\":");
  printJsonString(kBoardProfile.key);

  Serial.print(",\"board_name\":");
  printJsonString(kBoardProfile.displayName);

  Serial.print(",\"ha_enabled\":");
  Serial.print(homeAssistantConfigured() ? "true" : "false");

  Serial.print(",\"pins\":{\"radar_rx\":");
  Serial.print(RADAR_RX_PIN);
  Serial.print(",\"radar_tx\":");
  Serial.print(RADAR_TX_PIN);
  Serial.print(",\"presence\":");
  Serial.print(RADAR_PRESENCE_PIN);
  Serial.print(",\"status_rgb_led\":");
  Serial.print(kBoardProfile.hasStatusRgbLed ? "true" : "false");
  Serial.print("}");

  finishJsonEvent();
}

void emitHeartbeatEvent() {
  printJsonEventPrefix("heartbeat");

  Serial.print(",\"free_heap\":");
  Serial.print(ESP.getFreeHeap());

  Serial.print(",\"presence\":");
  Serial.print(lastPresence ? "true" : "false");

  Serial.print(",\"gpio_presence\":");
  Serial.print(lastGpioPresence ? "true" : "false");

  Serial.print(",\"detection_candidate\":");
  Serial.print(detectionCandidateActive ? "true" : "false");

  Serial.print(",\"presence_decay_remaining_ms\":");
  Serial.print(presenceDecayRemainingMs(millis()));

  Serial.print(",\"wifi_connected\":");
  Serial.print(WiFi.status() == WL_CONNECTED ? "true" : "false");

  Serial.print(",\"wifi_disconnect_reason\":");
  Serial.print(lastWiFiDisconnectReason);

  Serial.print(",\"wifi_disconnect_reason_text\":");
  printJsonString(lastWiFiDisconnectReasonText);

  Serial.print(",\"ip_address\":");
  printJsonString(lastKnownIpAddress);

  Serial.print(",\"mqtt_connected\":");
  Serial.print(HomeAssistantMqttClient.connected() ? "true" : "false");

  Serial.print(",\"mqtt_state\":");
  Serial.print(lastMqttState);

  Serial.print(",\"mqtt_state_text\":");
  printJsonString(lastMqttStateText);

  Serial.print(",\"mqtt_host_ip\":");
  printJsonString(lastResolvedMqttHost);

  Serial.print(",\"topic_prefix\":");
  printJsonString(homeAssistantDeviceTopic());

  Serial.print(",\"radar_bytes_total\":");
  Serial.print(radarBytesTotal);

  Serial.print(",\"radar_frames_total\":");
  Serial.print(radarFramesTotal);

  Serial.print(",\"ld2420_energy_frames_total\":");
  Serial.print(ld2420EnergyFramesTotal);

  Serial.print(",\"presence_changes_total\":");
  Serial.print(presenceChangesTotal);

  finishJsonEvent();
  broadcastDeviceSnapshot();
}

void emitPresenceEvent(bool presence, bool changed) {
  printJsonEventPrefix(changed ? "presence_changed" : "presence_sample");

  Serial.print(",\"presence\":");
  Serial.print(presence ? "true" : "false");

  Serial.print(",\"gpio_presence\":");
  Serial.print(lastGpioPresence ? "true" : "false");

  Serial.print(",\"detection_candidate\":");
  Serial.print(detectionCandidateActive ? "true" : "false");

  Serial.print(",\"pin\":");
  Serial.print(RADAR_PRESENCE_PIN);

  Serial.print(",\"raw\":");
  Serial.print(digitalRead(RADAR_PRESENCE_PIN));

  if (changed) {
    Serial.print(",\"changes_total\":");
    Serial.print(presenceChangesTotal);
  }

  Serial.print(",\"presence_decay_remaining_ms\":");
  Serial.print(presenceDecayRemainingMs(millis()));

  finishJsonEvent();
  broadcastDeviceSnapshot();
}

void emitCommandEvent(const String& command) {
  printJsonEventPrefix("usb_command");

  Serial.print(",\"command\":");
  printJsonString(command);

  finishJsonEvent();
}

void emitErrorEvent(const char* message) {
  printJsonEventPrefix("error");

  Serial.print(",\"message\":");
  printJsonString(message);

  finishJsonEvent();
}

void emitTextRangeFrame(const uint8_t* data, size_t length, const String& ascii) {
  radarFramesTotal++;

  bool presence = ascii.indexOf("ON") >= 0;
  int rangeStart = ascii.indexOf("Range");
  int range = -1;

  if (rangeStart >= 0) {
    String rangeText = ascii.substring(rangeStart + 5);
    rangeText.trim();
    range = rangeText.toInt();
  }

  updateDistanceCm(range);
  latestTextFrameSnapshot.valid = true;
  latestTextFrameSnapshot.length = length;
  latestTextFrameSnapshot.presence = presence;
  latestTextFrameSnapshot.range = range;
  latestTextFrameSnapshot.bytesTotal = radarBytesTotal;
  latestTextFrameSnapshot.framesTotal = radarFramesTotal;
  latestTextFrameSnapshot.hex = buildHexString(data, length);
  latestTextFrameSnapshot.ascii = ascii;

  printJsonEventPrefix("ld2420_text_range");

  Serial.print(",\"length\":");
  Serial.print(length);

  Serial.print(",\"bytes_total\":");
  Serial.print(radarBytesTotal);

  Serial.print(",\"frames_total\":");
  Serial.print(radarFramesTotal);

  Serial.print(",\"presence\":");
  Serial.print(presence ? "true" : "false");

  if (range >= 0) {
    Serial.print(",\"range\":");
    Serial.print(range);
  }

  Serial.print(",\"hex\":\"");
  for (size_t i = 0; i < length; i++) {
    printHexByte(data[i]);
  }
  Serial.print("\"");

  Serial.print(",\"ascii\":");
  printJsonString(ascii);

  finishJsonEvent();
  broadcastDeviceSnapshot();
}

// -----------------------------------------------------
// LD2420 frame parsing
// -----------------------------------------------------

bool startsWithEnergyHeader(const uint8_t* data, size_t len) {
  return len >= 4 &&
         data[0] == ENERGY_HEADER[0] &&
         data[1] == ENERGY_HEADER[1] &&
         data[2] == ENERGY_HEADER[2] &&
         data[3] == ENERGY_HEADER[3];
}

bool endsWithEnergyFooter(const uint8_t* data, size_t len) {
  return len >= 4 &&
         data[len - 4] == ENERGY_FOOTER[0] &&
         data[len - 3] == ENERGY_FOOTER[1] &&
         data[len - 2] == ENERGY_FOOTER[2] &&
         data[len - 1] == ENERGY_FOOTER[3];
}

uint16_t readLe16(const uint8_t* data, size_t offset) {
  return static_cast<uint16_t>(data[offset]) |
         (static_cast<uint16_t>(data[offset + 1]) << 8);
}

size_t decodeHexBytes(const char* hex, uint8_t* output, size_t capacity) {
  size_t length = strlen(hex);
  size_t outputLength = min(capacity, length / 2);

  auto hexNibble = [](char value) -> uint8_t {
    if (value >= '0' && value <= '9') return static_cast<uint8_t>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<uint8_t>(10 + (value - 'a'));
    if (value >= 'A' && value <= 'F') return static_cast<uint8_t>(10 + (value - 'A'));
    return 0;
  };

  for (size_t index = 0; index < outputLength; index++) {
    output[index] = static_cast<uint8_t>((hexNibble(hex[index * 2]) << 4) | hexNibble(hex[(index * 2) + 1]));
  }

  return outputLength;
}

void buildGenericFrameSnapshot(const uint8_t* data, size_t length, LatestGenericFrameSnapshot& snapshot) {
  snapshot.valid = true;
  snapshot.length = length;
  snapshot.bytesTotal = length;
  snapshot.framesTotal = 1;
  snapshot.hex = buildHexString(data, length);
  snapshot.ascii = "";
}

bool parseLd2420EnergyFrame(const uint8_t* data, size_t len, LatestEnergyFrameSnapshot& snapshot) {
  if (len < ENERGY_FRAME_LENGTH) {
    return false;
  }

  if (!startsWithEnergyHeader(data, len) || !endsWithEnergyFooter(data, len)) {
    return false;
  }

  snapshot.valid = true;
  snapshot.length = len;
  snapshot.payloadLength = readLe16(data, 4);
  snapshot.presence = data[6] != 0;
  snapshot.distanceCm = readLe16(data, 7);
  snapshot.bytesTotal = len;
  snapshot.framesTotal = 1;
  snapshot.energyFramesTotal = 1;

  for (size_t i = 0; i < LD2420_GATE_COUNT; i++) {
    const size_t offset = 9 + (i * 2);
    snapshot.gates[i] = readLe16(data, offset);
  }

  return true;
}

bool tryEmitEmbeddedLd2420EnergyFrames(const uint8_t* data, size_t len, size_t& consumedLength) {
  consumedLength = 0;

  if (len < ENERGY_FRAME_LENGTH) {
    return false;
  }

  bool emittedAny = false;
  size_t cursor = 0;

  // Network-heavy loops can coalesce several fixed-length LD2420 frames into one
  // UART buffer flush. Peel out complete embedded energy frames and keep any
  // trailing partial bytes for the next pass.
  while (cursor < len) {
    size_t remaining = len - cursor;
    if (remaining < ENERGY_FRAME_LENGTH) {
      break;
    }

    if (startsWithEnergyHeader(data + cursor, remaining) &&
        endsWithEnergyFooter(data + cursor, ENERGY_FRAME_LENGTH) &&
        tryEmitLd2420EnergyFrame(data + cursor, ENERGY_FRAME_LENGTH)) {
      emittedAny = true;
      cursor += ENERGY_FRAME_LENGTH;
      consumedLength = cursor;
      continue;
    }

    cursor++;
  }

  return emittedAny;
}

uint8_t classifyBenchmarkCommand(const String& command) {
  if (command == "ping") return 1;
  if (command == "status") return 2;
  if (command == "ha_status") return 3;
  if (command == "wifi_scan") return 4;
  if (command == "runtime_benchmark") return 5;
  if (command == "firmware_sync") return 6;
  if (command == "energy") return 7;
  if (command.startsWith("ha_config:")) return 8;
  if (command.startsWith("ha_room_config:")) return 9;
  if (command.startsWith("ha_room_pose_publish:")) return 10;
  if (command.startsWith("tuning_config:")) return 11;
  if (command.startsWith("ble_tag_config:")) return 12;
  if (command.startsWith("ble_tag_clear:")) return 13;
  if (command.startsWith("ha_ws_config:")) return 14;
  if (command.startsWith("ha_mqtt_endpoint:")) return 15;
  if (command.startsWith("firmware_update:")) return 16;
  if (command.startsWith("radar:")) return 17;
  return 0;
}

uint32_t parseBenchmarkRoomConfigPayload(const char* payload) {
  String fields[7];
  splitConfigFields(String(payload), fields, 7);

  uint32_t sink = 0;
  sink ^= static_cast<uint32_t>(fields[0].length());
  sink ^= static_cast<uint32_t>(fields[1].length() << 4);
  sink ^= static_cast<uint32_t>(constrain(fields[2].toInt(), -2000, 2000) + 2048);
  sink ^= static_cast<uint32_t>(constrain(fields[3].toInt(), -2000, 2000) + 4096);
  sink ^= static_cast<uint32_t>(constrain(fields[4].toInt(), -180, 180) + 8192);
  sink ^= static_cast<uint32_t>(constrain(fields[5].toInt(), 100, 4000));
  sink ^= static_cast<uint32_t>(constrain(fields[6].toInt(), 100, 4000) << 1);
  return sink;
}

uint32_t parseBenchmarkTuningConfigPayload(const char* payload) {
  String fields[8];
  splitConfigFields(String(payload), fields, 8);

  int parsedMaxRange = fields[0].toInt();
  int parsedMinEnergy = fields[1].toInt();
  int parsedSensitivity = fields[2].toInt();
  int parsedHoldMs = fields[3].toInt();
  int parsedMinGates = fields[4].toInt();
  int parsedMinActivity = fields[5].toInt();
  int parsedLedBrightness = fields[7].toInt();

  uint16_t maxRange = parsedMaxRange > 0 ? static_cast<uint16_t>(parsedMaxRange) : LD2420_GATE_COUNT * LD2420_GATE_SIZE_CM;
  uint16_t minEnergy = parsedMinEnergy > 0 ? static_cast<uint16_t>(parsedMinEnergy) : LD2420_ACTIVE_GATE_FLOOR;
  uint8_t sensitivity = static_cast<uint8_t>(constrain(parsedSensitivity > 0 ? parsedSensitivity : 55, 10, 100));
  uint16_t holdMs = parsedHoldMs >= 0
    ? static_cast<uint16_t>(constrain(parsedHoldMs, MIN_PRESENCE_HOLD_MS, MAX_PRESENCE_HOLD_MS))
    : DEFAULT_PRESENCE_HOLD_MS;
  uint8_t minGates = static_cast<uint8_t>(constrain(parsedMinGates > 0 ? parsedMinGates : 1, 1, LD2420_GATE_COUNT));
  uint8_t minActivity = static_cast<uint8_t>(constrain(parsedMinActivity > 0 ? parsedMinActivity : 10, 1, 100));
  bool ledEnabled = fields[6] == "1" || fields[6] == "true" || fields[6] == "on";
  uint8_t ledBrightness = static_cast<uint8_t>(constrain(parsedLedBrightness > 0 ? parsedLedBrightness : DEFAULT_LED_BRIGHTNESS, 1, 255));

  return static_cast<uint32_t>(maxRange)
    ^ (static_cast<uint32_t>(minEnergy) << 1)
    ^ (static_cast<uint32_t>(sensitivity) << 2)
    ^ (static_cast<uint32_t>(holdMs) << 3)
    ^ (static_cast<uint32_t>(minGates) << 4)
    ^ (static_cast<uint32_t>(minActivity) << 5)
    ^ (static_cast<uint32_t>(ledEnabled ? 1 : 0) << 6)
    ^ (static_cast<uint32_t>(ledBrightness) << 7);
}

void runRuntimeBenchmark() {
  uint8_t genericBytes[194] = {0};
  const size_t genericLength = decodeHexBytes(RUNTIME_BENCHMARK_GENERIC_FRAME_HEX,
                                              genericBytes,
                                              sizeof(genericBytes));

  LatestEnergyFrameSnapshot energySnapshot;
  energySnapshot.valid = true;
  energySnapshot.length = 45;
  energySnapshot.payloadLength = 35;
  energySnapshot.presence = false;
  energySnapshot.distanceCm = 0;
  energySnapshot.bytesTotal = energySnapshot.length;
  energySnapshot.framesTotal = 1;
  energySnapshot.energyFramesTotal = 1;
  for (size_t gateIndex = 0; gateIndex < LD2420_GATE_COUNT; gateIndex++) {
    energySnapshot.gates[gateIndex] = RUNTIME_BENCHMARK_ENERGY_GATES[gateIndex];
  }

  RuntimeHomeAssistantConfig benchmarkConfig;
  benchmarkConfig.enabled = false;
  benchmarkConfig.ledEnabled = false;

  volatile uint32_t commandParseSink = 0;
  volatile uint32_t roomConfigSink = 0;
  volatile uint32_t tuningConfigSink = 0;
  volatile size_t genericHexLengthSink = 0;
  volatile uint32_t totalGateEnergySink = 0;
  volatile bool detectionCandidateSink = false;

  uint32_t commandParseStartUs = micros();
  for (uint32_t iteration = 0; iteration < RUNTIME_BENCHMARK_ITERATIONS; iteration++) {
    commandParseSink ^= classifyBenchmarkCommand(RUNTIME_BENCHMARK_SIMPLE_COMMAND);
  }
  uint32_t commandParseElapsedUs = micros() - commandParseStartUs;

  uint32_t roomConfigStartUs = micros();
  for (uint32_t iteration = 0; iteration < RUNTIME_BENCHMARK_ITERATIONS; iteration++) {
    roomConfigSink ^= parseBenchmarkRoomConfigPayload(RUNTIME_BENCHMARK_ROOM_CONFIG_PAYLOAD);
  }
  uint32_t roomConfigElapsedUs = micros() - roomConfigStartUs;

  uint32_t tuningConfigStartUs = micros();
  for (uint32_t iteration = 0; iteration < RUNTIME_BENCHMARK_ITERATIONS; iteration++) {
    tuningConfigSink ^= parseBenchmarkTuningConfigPayload(RUNTIME_BENCHMARK_TUNING_CONFIG_PAYLOAD);
  }
  uint32_t tuningConfigElapsedUs = micros() - tuningConfigStartUs;

  uint32_t parseStartUs = micros();
  for (uint32_t iteration = 0; iteration < RUNTIME_BENCHMARK_ITERATIONS; iteration++) {
    LatestGenericFrameSnapshot genericSnapshot;
    buildGenericFrameSnapshot(genericBytes, genericLength, genericSnapshot);
    genericHexLengthSink ^= genericSnapshot.hex.length();
  }
  uint32_t parseElapsedUs = micros() - parseStartUs;

  RadarDerivedMetrics metrics;
  uint32_t metricsStartUs = micros();
  for (uint32_t iteration = 0; iteration < RUNTIME_BENCHMARK_ITERATIONS; iteration++) {
    metrics = buildRadarDerivedMetrics(&energySnapshot, nullptr, false);
    totalGateEnergySink ^= metrics.totalGateEnergy;
  }
  uint32_t metricsElapsedUs = micros() - metricsStartUs;

  metrics = buildRadarDerivedMetrics(&energySnapshot, nullptr, false);
  uint32_t detectionStartUs = micros();
  for (uint32_t iteration = 0; iteration < RUNTIME_BENCHMARK_ITERATIONS; iteration++) {
    detectionCandidateSink ^= radarDetectionCandidate(metrics, &energySnapshot, benchmarkConfig);
  }
  uint32_t detectionElapsedUs = micros() - detectionStartUs;

  latestRuntimeBenchmarkSnapshot.valid = true;
  latestRuntimeBenchmarkSnapshot.measuredAtMs = millis();
  latestRuntimeBenchmarkSnapshot.iterations = RUNTIME_BENCHMARK_ITERATIONS;
  latestRuntimeBenchmarkSnapshot.parseCommandFixture.totalUs = commandParseElapsedUs;
  latestRuntimeBenchmarkSnapshot.parseCommandFixture.perIterNs =
    static_cast<uint32_t>((static_cast<uint64_t>(commandParseElapsedUs) * 1000ULL) / RUNTIME_BENCHMARK_ITERATIONS);
  latestRuntimeBenchmarkSnapshot.parseRoomConfigFixture.totalUs = roomConfigElapsedUs;
  latestRuntimeBenchmarkSnapshot.parseRoomConfigFixture.perIterNs =
    static_cast<uint32_t>((static_cast<uint64_t>(roomConfigElapsedUs) * 1000ULL) / RUNTIME_BENCHMARK_ITERATIONS);
  latestRuntimeBenchmarkSnapshot.parseTuningConfigFixture.totalUs = tuningConfigElapsedUs;
  latestRuntimeBenchmarkSnapshot.parseTuningConfigFixture.perIterNs =
    static_cast<uint32_t>((static_cast<uint64_t>(tuningConfigElapsedUs) * 1000ULL) / RUNTIME_BENCHMARK_ITERATIONS);
  latestRuntimeBenchmarkSnapshot.parseGenericFixture.totalUs = parseElapsedUs;
  latestRuntimeBenchmarkSnapshot.parseGenericFixture.perIterNs =
    static_cast<uint32_t>((static_cast<uint64_t>(parseElapsedUs) * 1000ULL) / RUNTIME_BENCHMARK_ITERATIONS);
  latestRuntimeBenchmarkSnapshot.deriveMetricsFixture.totalUs = metricsElapsedUs;
  latestRuntimeBenchmarkSnapshot.deriveMetricsFixture.perIterNs =
    static_cast<uint32_t>((static_cast<uint64_t>(metricsElapsedUs) * 1000ULL) / RUNTIME_BENCHMARK_ITERATIONS);
  latestRuntimeBenchmarkSnapshot.detectionCandidateFixture.totalUs = detectionElapsedUs;
  latestRuntimeBenchmarkSnapshot.detectionCandidateFixture.perIterNs =
    static_cast<uint32_t>((static_cast<uint64_t>(detectionElapsedUs) * 1000ULL) / RUNTIME_BENCHMARK_ITERATIONS);
  latestRuntimeBenchmarkSnapshot.detectionCandidate = radarDetectionCandidate(metrics, &energySnapshot, benchmarkConfig);
  latestRuntimeBenchmarkSnapshot.peopleEstimate = metrics.estimatedPeople;
  latestRuntimeBenchmarkSnapshot.activeGateCount = metrics.activeGateCount;
  latestRuntimeBenchmarkSnapshot.activityScore = metrics.activityScore;
  latestRuntimeBenchmarkSnapshot.dominantGateDistanceCm = metrics.dominantGateDistanceCm;

  printJsonEventPrefix("runtime_benchmark");
  Serial.print(",\"iterations\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.iterations);
  Serial.print(",\"parse_command_fixture\":{");
  Serial.print("\"total_us\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.parseCommandFixture.totalUs);
  Serial.print(",\"per_iter_ns\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.parseCommandFixture.perIterNs);
  Serial.print("}");
  Serial.print(",\"parse_room_config_fixture\":{");
  Serial.print("\"total_us\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.parseRoomConfigFixture.totalUs);
  Serial.print(",\"per_iter_ns\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.parseRoomConfigFixture.perIterNs);
  Serial.print("}");
  Serial.print(",\"parse_tuning_config_fixture\":{");
  Serial.print("\"total_us\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.parseTuningConfigFixture.totalUs);
  Serial.print(",\"per_iter_ns\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.parseTuningConfigFixture.perIterNs);
  Serial.print("}");
  Serial.print(",\"parse_generic_fixture\":{");
  Serial.print("\"total_us\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.parseGenericFixture.totalUs);
  Serial.print(",\"per_iter_ns\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.parseGenericFixture.perIterNs);
  Serial.print("}");
  Serial.print(",\"derive_metrics_fixture\":{");
  Serial.print("\"total_us\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.deriveMetricsFixture.totalUs);
  Serial.print(",\"per_iter_ns\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.deriveMetricsFixture.perIterNs);
  Serial.print("}");
  Serial.print(",\"detection_candidate_fixture\":{");
  Serial.print("\"total_us\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.detectionCandidateFixture.totalUs);
  Serial.print(",\"per_iter_ns\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.detectionCandidateFixture.perIterNs);
  Serial.print("}");
  Serial.print(",\"detection_candidate\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.detectionCandidate ? "true" : "false");
  Serial.print(",\"people_estimate\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.peopleEstimate);
  Serial.print(",\"active_gate_count\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.activeGateCount);
  Serial.print(",\"activity_score\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.activityScore);
  Serial.print(",\"dominant_gate_distance_cm\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.dominantGateDistanceCm);
  Serial.print(",\"generic_hex_length_sink\":");
  Serial.print(static_cast<uint32_t>(genericHexLengthSink));
  Serial.print(",\"command_parse_sink\":");
  Serial.print(commandParseSink);
  Serial.print(",\"room_config_sink\":");
  Serial.print(roomConfigSink);
  Serial.print(",\"tuning_config_sink\":");
  Serial.print(tuningConfigSink);
  Serial.print(",\"total_gate_energy_sink\":");
  Serial.print(totalGateEnergySink);
  Serial.print(",\"detection_candidate_sink\":");
  Serial.print(detectionCandidateSink ? "true" : "false");
  finishJsonEvent();
  broadcastDeviceSnapshot();
}

bool tryEmitLd2420EnergyFrame(const uint8_t* data, size_t len) {
  LatestEnergyFrameSnapshot parsed;
  if (!parseLd2420EnergyFrame(data, len, parsed)) {
    return false;
  }

  radarFramesTotal++;
  ld2420EnergyFramesTotal++;

  updateDistanceCm(parsed.distanceCm);
  latestEnergyFrameSnapshot = parsed;
  latestEnergyFrameSnapshot.bytesTotal = radarBytesTotal;
  latestEnergyFrameSnapshot.framesTotal = radarFramesTotal;
  latestEnergyFrameSnapshot.energyFramesTotal = ld2420EnergyFramesTotal;
  latestTextFrameSnapshot.valid = false;

  printJsonEventPrefix("ld2420_energy");

  Serial.print(",\"length\":");
  Serial.print(len);

  Serial.print(",\"payload_length\":");
  Serial.print(parsed.payloadLength);

  Serial.print(",\"bytes_total\":");
  Serial.print(radarBytesTotal);

  Serial.print(",\"frames_total\":");
  Serial.print(radarFramesTotal);

  Serial.print(",\"energy_frames_total\":");
  Serial.print(ld2420EnergyFramesTotal);

  Serial.print(",\"presence\":");
  Serial.print(parsed.presence ? "true" : "false");

  Serial.print(",\"distance_cm\":");
  Serial.print(parsed.distanceCm);

  Serial.print(",\"gates\":[");

  for (size_t i = 0; i < 16; i++) {
    if (i > 0) {
      Serial.print(",");
    }

    Serial.print(parsed.gates[i]);
  }

  Serial.print("]");

  finishJsonEvent();
  broadcastDeviceSnapshot();

  return true;
}

bool tryEmitLd2420TextFrame(const uint8_t* data, size_t len) {
  if (len == 0) {
    return false;
  }

  String ascii;
  ascii.reserve(len + 1);

  bool allTextish = true;

  for (size_t i = 0; i < len; i++) {
    uint8_t b = data[i];

    if (b == '\r' || b == '\n' || b == '\t' || (b >= 32 && b <= 126)) {
      ascii += static_cast<char>(b);
    } else {
      allTextish = false;
      break;
    }
  }

  if (!allTextish) {
    return false;
  }

  if (ascii.indexOf("Range") < 0 && ascii.indexOf("ON") < 0 && ascii.indexOf("OFF") < 0) {
    return false;
  }

  emitTextRangeFrame(data, len, ascii);
  return true;
}

void emitRadarFrameEvent(const uint8_t* data, size_t length) {
  if (tryEmitLd2420EnergyFrame(data, length)) {
    return;
  }

  if (tryEmitLd2420TextFrame(data, length)) {
    return;
  }

  radarFramesTotal++;
  latestGenericFrameSnapshot.valid = true;
  latestGenericFrameSnapshot.length = length;
  latestGenericFrameSnapshot.bytesTotal = radarBytesTotal;
  latestGenericFrameSnapshot.framesTotal = radarFramesTotal;
  latestGenericFrameSnapshot.hex = buildHexString(data, length);
  latestGenericFrameSnapshot.ascii = "";

  printJsonEventPrefix("radar_uart_frame");

  Serial.print(",\"length\":");
  Serial.print(length);

  Serial.print(",\"bytes_total\":");
  Serial.print(radarBytesTotal);

  Serial.print(",\"frames_total\":");
  Serial.print(radarFramesTotal);

  Serial.print(",\"hex\":\"");
  for (size_t i = 0; i < length; i++) {
    printHexByte(data[i]);
  }
  Serial.print("\"");

  Serial.print(",\"ascii\":");
  Serial.print('"');
  for (size_t i = 0; i < length; i++) {
    uint8_t b = data[i];

    if (b >= 32 && b <= 126) {
      if (b == '"' || b == '\\') {
        Serial.print('\\');
      }
      Serial.print((char)b);
    } else if (b == '\r') {
      Serial.print("\\r");
    } else if (b == '\n') {
      Serial.print("\\n");
    } else if (b == '\t') {
      Serial.print("\\t");
    } else {
      Serial.print('.');
    }
  }
  Serial.print('"');

  latestGenericFrameSnapshot.ascii.reserve(length);
  for (size_t i = 0; i < length; i++) {
    uint8_t b = data[i];
    latestGenericFrameSnapshot.ascii += (b >= 32 && b <= 126) ? static_cast<char>(b) : '.';
  }

  finishJsonEvent();
  broadcastDeviceSnapshot();
}

// -----------------------------------------------------
// Radar UART handling
// -----------------------------------------------------

void flushRadarBufferIfNeeded(bool force) {
  if (radarBufferLength == 0) {
    return;
  }

  uint32_t now = millis();
  bool idleGapElapsed = (now - lastRadarByteMs) >= RADAR_IDLE_FRAME_GAP_MS;

  if (force || idleGapElapsed || radarBufferLength >= RADAR_FRAME_BUFFER_SIZE) {
    size_t consumedLength = 0;
    if (tryEmitEmbeddedLd2420EnergyFrames(radarBuffer, radarBufferLength, consumedLength)) {
      size_t retainedLength = radarBufferLength - consumedLength;
      if (retainedLength > 0) {
        memmove(radarBuffer, radarBuffer + consumedLength, retainedLength);
      }
      radarBufferLength = retainedLength;
      return;
    }

    emitRadarFrameEvent(radarBuffer, radarBufferLength);
    radarBufferLength = 0;
  }
}

void readRadarUart() {
  while (RadarSerial.available() > 0) {
    int incoming = RadarSerial.read();

    if (incoming < 0) {
      break;
    }

    radarBytesTotal++;
    lastRadarByteMs = millis();

    if (radarBufferLength < RADAR_FRAME_BUFFER_SIZE) {
      radarBuffer[radarBufferLength++] = static_cast<uint8_t>(incoming);
    } else {
      flushRadarBufferIfNeeded(true);
      radarBuffer[radarBufferLength++] = static_cast<uint8_t>(incoming);
    }
  }

  flushRadarBufferIfNeeded(false);
}

// -----------------------------------------------------
// Presence GPIO handling
// -----------------------------------------------------

void pollPresence() {
  uint32_t now = millis();

  if ((now - lastPresencePollMs) < PRESENCE_POLL_MS) {
    return;
  }

  lastPresencePollMs = now;

  lastGpioPresence = digitalRead(RADAR_PRESENCE_PIN) == HIGH;
  RadarDerivedMetrics metrics = buildRadarDerivedMetrics();
  bool radarCandidate = radarDetectionCandidate(metrics);
  bool gpioFallback = lastGpioPresence && !latestEnergyFrameSnapshot.valid && !latestTextFrameSnapshot.valid;
  detectionCandidateActive = radarCandidate || gpioFallback;

  if (detectionCandidateActive) {
    lastDetectionMs = now;
  }

  bool presence = detectionCandidateActive || presenceDecayRemainingMs(now) > 0;

  if (!presenceInitialized) {
    presenceInitialized = true;
    lastPresence = presence;
    emitPresenceEvent(presence, false);
    publishHomeAssistantPresence();
    return;
  }

  if (presence != lastPresence) {
    lastPresence = presence;
    presenceChangesTotal++;
    emitPresenceEvent(presence, true);
    publishHomeAssistantPresence();
  }
}

// -----------------------------------------------------
// USB command handling
//
// Type into serial monitor or the JS applet:
//   ping
//   status
//   ha_status
//   wifi_scan
//   ha_config:<ssid>|<password>|<mqtt_host>|<mqtt_port>|<mqtt_user>|<mqtt_password>|<node_id>|<friendly_name>
//   ha_room_config:<room_id>|<sensor_role>|<pose_x_cm>|<pose_y_cm>|<heading_deg>|<room_width_cm>|<room_height_cm>
//   ha_room_pose_publish:<node_id>|<room_id>|<sensor_role>|<pose_x_cm>|<pose_y_cm>|<heading_deg>|<room_width_cm>|<room_height_cm>
//   tuning_config:<max_range_cm>|<min_gate_energy>|<sensitivity_pct>|<presence_hold_ms>|<min_active_gates>|<min_activity_score>|<led_enabled>|<led_brightness>
//   ble_tag_config:<slot>|<label>|<ble_mac>|<min_rssi>
//   ble_tag_clear:<slot>
//   ha_ws_config:<enabled>|<path>|<host_header>
//   ha_mqtt_endpoint:<mqtt_host>|<mqtt_port>|<use_websockets>|<websocket_path>|<host_header>
//   firmware_sync
//   firmware_update:<version>
//   runtime_benchmark
//   energy
//   radar:hello
//
// "radar:<text>" forwards raw text to the LD2420 UART.
// -----------------------------------------------------

void handleUsbCommand(const String& command) {
  emitCommandEvent(command);

  if (command == "ping") {
    printJsonEventPrefix("pong");
    finishJsonEvent();
    return;
  }

  if (command == "status") {
    emitHeartbeatEvent();
    emitPresenceEvent(lastPresence, false);
    emitHomeAssistantConfigEvent();
    return;
  }

  if (command == "debug_status") {
    Serial.println(buildDebugStatusJson());
    return;
  }

  if (command == "ha_status") {
    emitHomeAssistantConfigEvent();
    return;
  }

  if (command == "wifi_scan") {
    emitWiFiScanResults();
    return;
  }

  if (command == "runtime_benchmark") {
    runRuntimeBenchmark();
    return;
  }

  if (command.startsWith("ha_config:")) {
    String payload = command.substring(strlen("ha_config:"));
    String fields[8];
    splitConfigFields(payload, fields, 8);

    runtimeConfig.enabled = true;
    runtimeConfig.wifiSsid = fields[0];
    runtimeConfig.wifiPassword = fields[1];
    runtimeConfig.mqttHost = fields[2];

    int parsedPort = fields[3].toInt();
    runtimeConfig.mqttPort = parsedPort > 0 ? static_cast<uint16_t>(parsedPort) : HomeAssistantConfig::kMqttPort;

    runtimeConfig.mqttUsername = fields[4];
    runtimeConfig.mqttPassword = fields[5];
    runtimeConfig.mqttUseWebSockets = runtimeConfig.mqttUseWebSockets || mqttEndpointUsesWebSockets(runtimeConfig.mqttHost);
    runtimeConfig.mqttWebSocketPath = normalizeWebSocketPath(runtimeConfig.mqttWebSocketPath);
    runtimeConfig.nodeId = fields[6].length() > 0 ? fields[6] : defaultNodeId();
    runtimeConfig.friendlyName = fields[7].length() > 0 ? fields[7] : defaultFriendlyName();
    if (runtimeConfig.roomId.length() == 0) {
      runtimeConfig.roomId = defaultRoomId();
    }
    if (runtimeConfig.sensorRole.length() == 0) {
      runtimeConfig.sensorRole = defaultSensorRole();
    }

    saveHomeAssistantConfig();
    resetHomeAssistantConnections();

    printJsonEventPrefix("ha_config_saved");
    Serial.print(",\"configured\":");
    Serial.print(homeAssistantConfigured() ? "true" : "false");
    finishJsonEvent();

    emitHomeAssistantConfigEvent();
    return;
  }

  if (command.startsWith("ha_room_config:")) {
    String payload = command.substring(strlen("ha_room_config:"));
    String fields[7];
    splitConfigFields(payload, fields, 7);

    applyRoomPoseConfig(fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], fields[6], true, true, "ha_room_config_saved");
    return;
  }

  if (command.startsWith("ha_room_pose_publish:")) {
    String payload = command.substring(strlen("ha_room_pose_publish:"));
    String fields[8];
    splitConfigFields(payload, fields, 8);

    bool published = publishRoomPoseCommand(fields[0],
                                            fields[1],
                                            fields[2],
                                            static_cast<int16_t>(constrain(fields[3].toInt(), -2000, 2000)),
                                            static_cast<int16_t>(constrain(fields[4].toInt(), -2000, 2000)),
                                            static_cast<int16_t>(constrain(fields[5].toInt(), -180, 180)),
                                            static_cast<uint16_t>(constrain(fields[6].toInt(), 100, 4000)),
                                            static_cast<uint16_t>(constrain(fields[7].toInt(), 100, 4000)));

    printJsonEventPrefix("ha_room_pose_publish");
    Serial.print(",\"node_id\":");
    printJsonString(fields[0]);
    Serial.print(",\"published\":");
    Serial.print(published ? "true" : "false");
    finishJsonEvent();
    return;
  }

  if (command.startsWith("tuning_config:")) {
    String payload = command.substring(strlen("tuning_config:"));
    String fields[8];
    splitConfigFields(payload, fields, 8);

    int parsedMaxRange = fields[0].toInt();
    int parsedMinEnergy = fields[1].toInt();
    int parsedSensitivity = fields[2].toInt();
    int parsedHoldMs = fields[3].toInt();
    int parsedMinGates = fields[4].toInt();
    int parsedMinActivity = fields[5].toInt();
    int parsedLedBrightness = fields[7].toInt();

    runtimeConfig.maxDetectionRangeCm = parsedMaxRange > 0 ? static_cast<uint16_t>(parsedMaxRange) : LD2420_GATE_COUNT * LD2420_GATE_SIZE_CM;
    runtimeConfig.minGateEnergy = parsedMinEnergy > 0 ? static_cast<uint16_t>(parsedMinEnergy) : LD2420_ACTIVE_GATE_FLOOR;
    runtimeConfig.sensitivityPercent = static_cast<uint8_t>(constrain(parsedSensitivity > 0 ? parsedSensitivity : 55, 10, 100));
    runtimeConfig.presenceHoldMs = parsedHoldMs >= 0
      ? static_cast<uint16_t>(constrain(parsedHoldMs, MIN_PRESENCE_HOLD_MS, MAX_PRESENCE_HOLD_MS))
      : DEFAULT_PRESENCE_HOLD_MS;
    runtimeConfig.minActiveGates = static_cast<uint8_t>(constrain(parsedMinGates > 0 ? parsedMinGates : 1, 1, LD2420_GATE_COUNT));
    runtimeConfig.minActivityScore = static_cast<uint8_t>(constrain(parsedMinActivity > 0 ? parsedMinActivity : 10, 1, 100));
    runtimeConfig.ledEnabled = fields[6] == "1" || fields[6] == "true" || fields[6] == "on";
    runtimeConfig.ledBrightness = static_cast<uint8_t>(constrain(parsedLedBrightness > 0 ? parsedLedBrightness : DEFAULT_LED_BRIGHTNESS, 1, 255));

    saveHomeAssistantConfig();

    printJsonEventPrefix("tuning_config_saved");
    Serial.print(",\"max_detection_range_cm\":");
    Serial.print(runtimeConfig.maxDetectionRangeCm);
    Serial.print(",\"min_gate_energy\":");
    Serial.print(runtimeConfig.minGateEnergy);
    Serial.print(",\"sensitivity_percent\":");
    Serial.print(runtimeConfig.sensitivityPercent);
    Serial.print(",\"presence_hold_ms\":");
    Serial.print(runtimeConfig.presenceHoldMs);
    Serial.print(",\"min_active_gates\":");
    Serial.print(runtimeConfig.minActiveGates);
    Serial.print(",\"min_activity_score\":");
    Serial.print(runtimeConfig.minActivityScore);
    Serial.print(",\"led_enabled\":");
    Serial.print(runtimeConfig.ledEnabled ? "true" : "false");
    Serial.print(",\"led_brightness\":");
    Serial.print(runtimeConfig.ledBrightness);
    finishJsonEvent();

    emitHomeAssistantConfigEvent();
    updateStatusRgbLed(millis());
    return;
  }

  if (command.startsWith("ble_tag_config:")) {
    String payload = command.substring(strlen("ble_tag_config:"));
    String fields[4];
    splitConfigFields(payload, fields, 4);

    int slotValue = fields[0].toInt();
    if (slotValue < 0 || slotValue >= MAX_BLE_TAGS) {
      emitErrorEvent("invalid_ble_tag_slot");
      return;
    }

    BleIdentityTag& tag = bleIdentityTags[slotValue];
    tag.label = fields[1];
    tag.address = normalizeBleIdentityValue(fields[2]);
    const int parsedMinRssi = fields[3].length() > 0 ? fields[3].toInt() : BLE_TAG_DEFAULT_MIN_RSSI;
    tag.minRssi = constrain(parsedMinRssi, -120, -20);
    tag.lastRssi = -127;
    tag.lastSeenMs = 0;
    tag.occupied = tag.label.length() > 0 && tag.address.length() > 0;
    if (!tag.occupied) {
      clearBleIdentityTag(static_cast<uint8_t>(slotValue));
    }

    saveHomeAssistantConfig();
    homeAssistantDiscoveryPublished = false;
    if (HomeAssistantMqttClient.connected()) {
      publishHomeAssistantDiscovery();
      publishBleStates();
    }

    printJsonEventPrefix("ble_tag_config_saved");
    Serial.print(",\"slot\":");
    Serial.print(slotValue);
    Serial.print(",\"occupied\":");
    Serial.print(tag.occupied ? "true" : "false");
    Serial.print(",\"label\":");
    printJsonString(tag.label);
    Serial.print(",\"address\":");
    printJsonString(tag.address);
    Serial.print(",\"min_rssi\":");
    Serial.print(tag.minRssi);
    finishJsonEvent();
    return;
  }

  if (command.startsWith("ble_tag_clear:")) {
    int slotValue = command.substring(strlen("ble_tag_clear:")).toInt();
    if (slotValue < 0 || slotValue >= MAX_BLE_TAGS) {
      emitErrorEvent("invalid_ble_tag_slot");
      return;
    }

    clearBleIdentityTag(static_cast<uint8_t>(slotValue));
    saveHomeAssistantConfig();
    homeAssistantDiscoveryPublished = false;
    if (HomeAssistantMqttClient.connected()) {
      publishHomeAssistantDiscovery();
      publishBleStates();
    }

    printJsonEventPrefix("ble_tag_config_saved");
    Serial.print(",\"slot\":");
    Serial.print(slotValue);
    Serial.print(",\"occupied\":false");
    finishJsonEvent();
    return;
  }

  if (command.startsWith("ha_ws_config:")) {
    String payload = command.substring(strlen("ha_ws_config:"));
    String fields[3];
    splitConfigFields(payload, fields, 3);

    runtimeConfig.mqttUseWebSockets = fields[0] == "1" || fields[0] == "true" || fields[0] == "on";
    runtimeConfig.mqttWebSocketPath = fields[1].length() > 0 ? normalizeWebSocketPath(fields[1]) : String(HomeAssistantConfig::kMqttWebSocketPath);
    runtimeConfig.mqttHostHeader = fields[2];

    saveHomeAssistantConfig();
    resetHomeAssistantConnections();

    printJsonEventPrefix("ha_ws_config_saved");
    Serial.print(",\"enabled\":");
    Serial.print(runtimeConfig.mqttUseWebSockets ? "true" : "false");
    finishJsonEvent();

    emitHomeAssistantConfigEvent();
    return;
  }

  if (command.startsWith("ha_mqtt_endpoint:")) {
    String payload = command.substring(strlen("ha_mqtt_endpoint:"));
    String fields[5];
    splitConfigFields(payload, fields, 5);

    if (fields[0].length() > 0) {
      runtimeConfig.mqttHost = fields[0];
    }

    int parsedPort = fields[1].toInt();
    if (parsedPort > 0) {
      runtimeConfig.mqttPort = static_cast<uint16_t>(parsedPort);
    }

    if (fields[2].length() > 0) {
      runtimeConfig.mqttUseWebSockets = fields[2] == "1" || fields[2] == "true" || fields[2] == "on";
    }

    if (fields[3].length() > 0) {
      runtimeConfig.mqttWebSocketPath = normalizeWebSocketPath(fields[3]);
    }

    runtimeConfig.mqttHostHeader = fields[4];

    saveHomeAssistantConfig();
    resetHomeAssistantConnections();

    printJsonEventPrefix("ha_mqtt_endpoint_saved");
    Serial.print(",\"mqtt_host\":");
    printJsonString(runtimeConfig.mqttHost);
    Serial.print(",\"mqtt_port\":");
    Serial.print(runtimeConfig.mqttPort);
    Serial.print(",\"mqtt_transport\":");
    printJsonString(runtimeConfig.mqttUseWebSockets ? "websocket" : "tcp");
    finishJsonEvent();

    emitHomeAssistantConfigEvent();
    return;
  }

  if (command == "firmware_sync") {
    String nodeId;
    String version;
    String source;
    const String localCore = semanticVersionCore(firmwareVersion());

    if (!findHighestPeerReleaseVersion(nodeId, version, source)) {
      firmwareSyncState.lastSuccess = false;
      firmwareSyncState.lastError = "no_peer_release_candidate";
      firmwareSyncState.statusText = "No peer release version is available to sync from.";
      firmwareSyncState.lastCompletedMs = millis();
      emitErrorEvent("firmware_sync_no_peer_release_candidate");
      return;
    }

    if (compareSemanticVersions(version, localCore) <= 0) {
      firmwareSyncState.lastSuccess = true;
      firmwareSyncState.lastError = "";
      firmwareSyncState.statusText = "Already on the highest visible peer release.";
      firmwareSyncState.lastCompletedMs = millis();

      printJsonEventPrefix("firmware_sync_not_needed");
      Serial.print(",\"local_version\":");
      printJsonString(localCore);
      Serial.print(",\"peer_version\":");
      printJsonString(version);
      finishJsonEvent();
      return;
    }

    requestFirmwareUpdate(version, nodeId, source);
    return;
  }

  if (command.startsWith("firmware_update:")) {
    requestFirmwareUpdate(command.substring(strlen("firmware_update:")), "manual", "manual");
    return;
  }

  if (command == "energy") {
    configureLd2420EnergyMode();
    return;
  }

  if (command.startsWith("radar:")) {
    String payload = command.substring(strlen("radar:"));
    RadarSerial.print(payload);

    printJsonEventPrefix("radar_write");
    Serial.print(",\"length\":");
    Serial.print(payload.length());
    Serial.print(",\"payload\":");
    printJsonString(payload);
    finishJsonEvent();
    return;
  }

  emitErrorEvent("unknown command");
}

void readUsbCommands() {
#if !ESPWAVERIDER_USB_CONSOLE
  return;
#else
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      usbCommandBuffer.trim();

      if (usbCommandBuffer.length() > 0) {
        handleUsbCommand(usbCommandBuffer);
      }

      usbCommandBuffer = "";
      return;
    }

    if (usbCommandBuffer.length() < 256) {
      usbCommandBuffer += c;
    } else {
      usbCommandBuffer = "";
      emitErrorEvent("usb command buffer overflow");
    }
  }
#endif
}

// -----------------------------------------------------
// LD2420 configuration commands
// -----------------------------------------------------

void writeRadarCommand(const uint8_t* data, size_t len, const char* name) {
  RadarSerial.write(data, len);
  RadarSerial.flush();

  printJsonEventPrefix("radar_command_sent");
  Serial.print(",\"name\":");
  printJsonString(name);
  Serial.print(",\"length\":");
  Serial.print(len);
  finishJsonEvent();

  delay(150);
}

void configureLd2420EnergyMode() {
  static const uint8_t enableConfig[] = {
    0xFD, 0xFC, 0xFB, 0xFA,
    0x04, 0x00,
    0xFF, 0x00,
    0x02, 0x00,
    0x04, 0x03, 0x02, 0x01
  };

  static const uint8_t setEnergyMode[] = {
    0xFD, 0xFC, 0xFB, 0xFA,
    0x08, 0x00,
    0x12, 0x00,
    0x00, 0x00,
    0x04, 0x00,
    0x00, 0x00,
    0x04, 0x03, 0x02, 0x01
  };

  static const uint8_t disableConfig[] = {
    0xFD, 0xFC, 0xFB, 0xFA,
    0x02, 0x00,
    0xFE, 0x00,
    0x04, 0x03, 0x02, 0x01
  };

  writeRadarCommand(enableConfig, sizeof(enableConfig), "enable_config");
  writeRadarCommand(setEnergyMode, sizeof(setEnergyMode), "set_energy_mode");
  writeRadarCommand(disableConfig, sizeof(disableConfig), "disable_config");
}

// -----------------------------------------------------
// Arduino lifecycle
// -----------------------------------------------------

void setup() {
  bootMillis = millis();

#if ESPWAVERIDER_USB_CONSOLE
  Serial.begin(USB_BAUD);
#endif
  SettingsStore.begin("ha", false);
  loadHomeAssistantConfig();
  HomeAssistantMqttClient.setCallback(handleMqttMessage);
  WiFi.onEvent(handleWiFiEvent);
  ensureAccessPointActive();
  ensureDeviceWebServerActive();
  ensureDeviceWebSocketActive();
  initializeBleScanner();

#if ESPWAVERIDER_USB_CONSOLE
  // Give USB CDC a moment to attach, but do not block forever.
  uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 2500) {
    delay(10);
  }
#endif

  pinMode(RADAR_PRESENCE_PIN, RADAR_PRESENCE_PIN_MODE);
  if (kBoardProfile.hasStatusRgbLed) {
    StatusRgbLed.begin();
    StatusRgbLed.clear();
    StatusRgbLed.show();
  }

  RadarSerial.begin(
    RADAR_BAUD,
    SERIAL_8N1,
    RADAR_RX_PIN,
    RADAR_TX_PIN
  );

  delay(500);

  if (homeAssistantConfigured()) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setHostname(deviceHostname().c_str());
    HomeAssistantMqttClient.setBufferSize(1024);
  }

  ensureMdnsActive();

  emitBootEvent();
  lastGpioPresence = digitalRead(RADAR_PRESENCE_PIN) == HIGH;
  emitPresenceEvent(lastGpioPresence, false);

  configureLd2420EnergyMode();

  lastHeartbeatMs = millis();
  lastPresencePollMs = millis();
  updateStatusRgbLed(millis());
}

void loop() {
  readRadarUart();
  pollPresence();
  readUsbCommands();
  ensureAccessPointActive();
  ensureMdnsActive();
  handleDeviceWebServer();
  ensureBleScannerActive();
  ensureWiFiConnected();
  serviceFirmwareSync();
  serviceUdpDiscovery();
  ensureMqttConnected();

  uint32_t now = millis();
  updateStatusRgbLed(now);

  if ((now - lastHeartbeatMs) >= HEARTBEAT_MS) {
    lastHeartbeatMs = now;
    emitHeartbeatEvent();
    publishHomeAssistantDiagnostics();
  }
  uint8_t genericBytes[194] = {0};
  const size_t genericLength = decodeHexBytes(RUNTIME_BENCHMARK_GENERIC_FRAME_HEX,
                                              genericBytes,
                                              sizeof(genericBytes));

  LatestEnergyFrameSnapshot energySnapshot;
  energySnapshot.valid = true;
  energySnapshot.length = 45;
  energySnapshot.payloadLength = 35;
  energySnapshot.presence = false;
  energySnapshot.distanceCm = 0;
  energySnapshot.bytesTotal = energySnapshot.length;
  energySnapshot.framesTotal = 1;
  energySnapshot.energyFramesTotal = 1;
  for (size_t gateIndex = 0; gateIndex < LD2420_GATE_COUNT; gateIndex++) {
    energySnapshot.gates[gateIndex] = RUNTIME_BENCHMARK_ENERGY_GATES[gateIndex];
  }

  RuntimeHomeAssistantConfig benchmarkConfig;
  benchmarkConfig.enabled = false;
  benchmarkConfig.ledEnabled = false;

  volatile size_t genericHexLengthSink = 0;
  volatile uint32_t totalGateEnergySink = 0;
  volatile bool detectionCandidateSink = false;

  uint32_t parseStartUs = micros();
  for (uint32_t iteration = 0; iteration < RUNTIME_BENCHMARK_ITERATIONS; iteration++) {
    LatestGenericFrameSnapshot genericSnapshot;
    buildGenericFrameSnapshot(genericBytes, genericLength, genericSnapshot);
    genericHexLengthSink ^= genericSnapshot.hex.length();
  }
  uint32_t parseElapsedUs = micros() - parseStartUs;

  RadarDerivedMetrics metrics;
  uint32_t metricsStartUs = micros();
  for (uint32_t iteration = 0; iteration < RUNTIME_BENCHMARK_ITERATIONS; iteration++) {
    metrics = buildRadarDerivedMetrics(&energySnapshot, nullptr, false);
    totalGateEnergySink ^= metrics.totalGateEnergy;
  }
  uint32_t metricsElapsedUs = micros() - metricsStartUs;

  metrics = buildRadarDerivedMetrics(&energySnapshot, nullptr, false);
  uint32_t detectionStartUs = micros();
  for (uint32_t iteration = 0; iteration < RUNTIME_BENCHMARK_ITERATIONS; iteration++) {
    detectionCandidateSink ^= radarDetectionCandidate(metrics, &energySnapshot, benchmarkConfig);
  }
  uint32_t detectionElapsedUs = micros() - detectionStartUs;

  latestRuntimeBenchmarkSnapshot.valid = true;
  latestRuntimeBenchmarkSnapshot.measuredAtMs = millis();
  latestRuntimeBenchmarkSnapshot.iterations = RUNTIME_BENCHMARK_ITERATIONS;
  latestRuntimeBenchmarkSnapshot.parseGenericFixture.totalUs = parseElapsedUs;
  latestRuntimeBenchmarkSnapshot.parseGenericFixture.perIterNs =
    static_cast<uint32_t>((static_cast<uint64_t>(parseElapsedUs) * 1000ULL) / RUNTIME_BENCHMARK_ITERATIONS);
  latestRuntimeBenchmarkSnapshot.deriveMetricsFixture.totalUs = metricsElapsedUs;
  latestRuntimeBenchmarkSnapshot.deriveMetricsFixture.perIterNs =
    static_cast<uint32_t>((static_cast<uint64_t>(metricsElapsedUs) * 1000ULL) / RUNTIME_BENCHMARK_ITERATIONS);
  latestRuntimeBenchmarkSnapshot.detectionCandidateFixture.totalUs = detectionElapsedUs;
  latestRuntimeBenchmarkSnapshot.detectionCandidateFixture.perIterNs =
    static_cast<uint32_t>((static_cast<uint64_t>(detectionElapsedUs) * 1000ULL) / RUNTIME_BENCHMARK_ITERATIONS);
  latestRuntimeBenchmarkSnapshot.detectionCandidate = radarDetectionCandidate(metrics, &energySnapshot, benchmarkConfig);
  latestRuntimeBenchmarkSnapshot.peopleEstimate = metrics.estimatedPeople;
  latestRuntimeBenchmarkSnapshot.activeGateCount = metrics.activeGateCount;
  latestRuntimeBenchmarkSnapshot.activityScore = metrics.activityScore;
  latestRuntimeBenchmarkSnapshot.dominantGateDistanceCm = metrics.dominantGateDistanceCm;

  printJsonEventPrefix("runtime_benchmark");
  Serial.print(",\"iterations\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.iterations);
  Serial.print(",\"parse_generic_fixture\":{");
  Serial.print("\"total_us\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.parseGenericFixture.totalUs);
  Serial.print(",\"per_iter_ns\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.parseGenericFixture.perIterNs);
  Serial.print("}");
  Serial.print(",\"derive_metrics_fixture\":{");
  Serial.print("\"total_us\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.deriveMetricsFixture.totalUs);
  Serial.print(",\"per_iter_ns\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.deriveMetricsFixture.perIterNs);
  Serial.print("}");
  Serial.print(",\"detection_candidate_fixture\":{");
  Serial.print("\"total_us\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.detectionCandidateFixture.totalUs);
  Serial.print(",\"per_iter_ns\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.detectionCandidateFixture.perIterNs);
  Serial.print("}");
  Serial.print(",\"detection_candidate\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.detectionCandidate ? "true" : "false");
  Serial.print(",\"people_estimate\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.peopleEstimate);
  Serial.print(",\"active_gate_count\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.activeGateCount);
  Serial.print(",\"activity_score\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.activityScore);
  Serial.print(",\"dominant_gate_distance_cm\":");
  Serial.print(latestRuntimeBenchmarkSnapshot.dominantGateDistanceCm);
  Serial.print(",\"generic_hex_length_sink\":");
  Serial.print(static_cast<uint32_t>(genericHexLengthSink));
  Serial.print(",\"total_gate_energy_sink\":");
  Serial.print(totalGateEnergySink);
  Serial.print(",\"detection_candidate_sink\":");
  Serial.print(detectionCandidateSink ? "true" : "false");
  finishJsonEvent();
  broadcastDeviceSnapshot();
}