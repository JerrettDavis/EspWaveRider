# HTTP And Commands

## HTTP endpoints

The device web server exposes the following primary endpoints:

- `GET /`: embedded dashboard.
- `GET /api/snapshot`: current device snapshot as JSON.
- `POST /api/command`: execute a text command and return JSON for that command. Most commands return the refreshed snapshot; some commands return a command-specific payload.

Snapshot polling is the compatibility path. The dashboard also upgrades to a live device WebSocket on port `81` when available.

## Snapshot endpoint

Example:

```powershell
Invoke-RestMethod 'http://device-host-or-ip/api/snapshot' | ConvertTo-Json -Depth 8
```

The snapshot includes configuration, Wi-Fi status, MQTT state, firmware metadata, live radar metrics, peer summaries, BLE observations, firmware sync state, and the latest on-device runtime benchmark result when one has been run.

## Command endpoint

Example:

```powershell
Invoke-RestMethod 'http://device-host-or-ip/api/command' -Method Post -ContentType 'text/plain' -Body 'status'
```

`wifi_scan` returns a dedicated `wifi_scan_results` payload with `count` and `networks` instead of the normal snapshot body:

```powershell
Invoke-RestMethod 'http://device-host-or-ip/api/command' -Method Post -ContentType 'text/plain' -Body 'wifi_scan'
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
- `runtime_benchmark`
- `energy`
- `radar:<text>`

`runtime_benchmark` runs a fixed-fixture on-device benchmark for the three parity slices used by the Rust port work:

- generic frame parsing
- derived metrics construction
- detection-candidate evaluation

Commands that return the refreshed snapshot include a `runtime_benchmark` object with `iterations`, per-slice `total_us`, and `per_iter_ns` values for direct comparison against the Rust host and Rust device benchmark output.

## WebSocket behavior

The dashboard prefers a WebSocket connection on port `81` for lower-latency live updates. If the socket is unavailable, it falls back to polling `GET /api/snapshot`.

## Safety notes

- The command endpoint can change Wi-Fi, MQTT, room, and tuning state.
- Treat it as an operator surface, not a public internet API.
- OTA should only be triggered when the device can reach the trusted release source cleanly.
