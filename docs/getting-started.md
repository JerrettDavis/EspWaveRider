# Getting Started

This guide is the shortest path from a fresh clone to a provisioned node with a working dashboard, snapshot API, MQTT link, and board-specific release identity.

## 1. Pick the right target

| Board | PlatformIO environment | Published release target |
| --- | --- | --- |
| ESP32-S3 DevKitM-1 primary test board | `esp32-s3-devkitm-1` | `lonely-esp32-s3-devkitm-1` |
| Generic ESP32 UART1 example board | `esp32dev-uart1` | `esp32-generic-uart1` |
| Heltec WiFi LoRa 32 V3 | `heltec-wifi-lora-32-v3` | `heltec-wifi-lora-32-v3` |
| Heltec WiFi LoRa 32 V4-compatible profile | `heltec-wifi-lora-32-v4` | `heltec-wifi-lora-32-v4-compatible` |

If you are flashing a GitHub release binary, choose the exact published release target for your board.

## 2. Install prerequisites

- `npm ci` from the repository root
- PlatformIO CLI or the PlatformIO VS Code extension
- Python 3.x
- Node.js
- USB data cable to the target board

Windows users in this workspace should use:

```powershell
C:\Users\jd\.platformio\penv\Scripts\platformio.exe
```

## 3. Build and flash

Primary board example:

```powershell
C:\Users\jd\.platformio\penv\Scripts\platformio.exe run --environment esp32-s3-devkitm-1 --target upload --upload-port COM17
```

Build-only example:

```powershell
C:\Users\jd\.platformio\penv\Scripts\platformio.exe run --environment esp32-s3-devkitm-1
```

## 4. Reach the node

After first boot, use one of these paths:

- Open the hosted dashboard at `http://device-host-or-ip/`
- Fetch `http://device-host-or-ip/api/snapshot`
- Use serial commands if the node is not provisioned yet

If the node is unconfigured, use the fallback AP path and provision Wi-Fi from the dashboard.

## 5. Provision the node

Minimum runtime configuration:

1. Wi-Fi SSID and password
2. MQTT host, port, and credentials if used
3. Stable `node_id` and friendly name
4. Room placement and role
5. Radar tuning if defaults are not appropriate for the installation

Equivalent raw command example:

```text
ha_config:<ssid>|<password>|<mqtt_host>|<mqtt_port>|<mqtt_user>|<mqtt_password>|<node_id>|<friendly_name>
```

Room placement example:

```text
ha_room_config:room-lab|fixed|120|40|90|700|500
```

## 6. Validate the install

Check the snapshot:

```powershell
Invoke-RestMethod 'http://device-host-or-ip/api/snapshot' | ConvertTo-Json -Depth 8
```

Look for these fields before calling the node ready:

- `wifi.connected=true`
- `mqtt_connected=true` when MQTT is configured
- expected `node_id`, `friendly_name`, and room placement values
- correct firmware version, build target, and git SHA
- peer discovery data if another node is already online

## 7. Validate release identity

The node reports its board-specific build target in the snapshot and uses that target to resolve OTA release assets.

Examples:

- `lonely-esp32-s3-devkitm-1`
- `esp32-generic-uart1`
- `heltec-wifi-lora-32-v4-compatible`

That field must match the published GitHub release asset suffix for safe OTA.

## 8. Common next steps

- Use `wifi_scan` to confirm RF visibility from the install location
- Use the room editor to place the node precisely in centimeters and degrees
- Use `runtime_benchmark` when comparing a board, branch, or runtime change
- Check peer discovery and room-summary surfaces before deploying more than one node

## 9. Known limits

- C++ is the current production baseline across the full shipping feature set.
- Rust is already strong on command/snapshot parity, MQTT over TCP, UDP discovery, and room collaboration in the default build.
- Rust OTA apply is feature-gated behind `https-ota` and is not yet validated as a production-safe release path.
- Rust live BLE observation is feature-gated behind `ble-scan` and is not yet stable enough to leave enabled on the current hardware.

For the current validated status, see [Parity Matrix](parity-matrix.md) and [Benchmarks And Comparison](benchmarks-and-comparison.md).