# HTTP And Commands

## HTTP endpoints

The device web server exposes the following primary endpoints:

- `GET /`: embedded dashboard.
- `GET /api/snapshot`: current device snapshot as JSON.
- `POST /api/command`: execute a text command and return the refreshed snapshot.

Snapshot polling is the compatibility path. The dashboard also upgrades to a live device WebSocket on port `81` when available.

## Snapshot endpoint

Example:

```powershell
Invoke-RestMethod 'http://device-host-or-ip/api/snapshot' | ConvertTo-Json -Depth 8
```

The snapshot includes configuration, Wi-Fi status, MQTT state, firmware metadata, live radar metrics, peer summaries, BLE observations, and firmware sync state.

## Command endpoint

Example:

```powershell
Invoke-RestMethod 'http://device-host-or-ip/api/command' -Method Post -ContentType 'text/plain' -Body 'status'
```

Supported command families include:

- `ping`
- `status`
- `ha_status`
- `wifi_scan`
- `ha_config:...`
- `ha_room_config:...`
- `ha_room_pose_publish:...`
- `tuning_config:...`
- `ble_tag_config:...`
- `ble_tag_clear:...`
- `ha_ws_config:...`
- `ha_mqtt_endpoint:...`
- `firmware_sync`
- `firmware_update:<version>`
- `energy`
- `radar:<text>`

## WebSocket behavior

The dashboard prefers a WebSocket connection on port `81` for lower-latency live updates. If the socket is unavailable, it falls back to polling `GET /api/snapshot`.

## Safety notes

- The command endpoint can change Wi-Fi, MQTT, room, and tuning state.
- Treat it as an operator surface, not a public internet API.
- OTA should only be triggered when the device can reach the trusted release source cleanly.
