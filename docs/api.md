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

Representative snapshot fields from a live node:

```json
{
	"firmware_version": "v1.0.2-2-g80b7e92-dirty",
	"build_target": "lonely-esp32-s3-devkitm-1",
	"node_id": "lb_mmwave_presence_test1",
	"wifi_connected": true,
	"mqtt_connected": true,
	"ip_address": "10.0.107.148",
	"dashboard_url": "http://lb-mmwave-presence-test1.local/",
	"presence": true,
	"people_estimate": 1,
	"room_peer_nodes": 1,
	"ble_beacon_count": 16
}
```

The example above is normalized to the board-specific release-target naming used by current GitHub releases.

Fields operators usually check first:

- identity: `firmware_version`, `build_target`, `git_sha`, `node_id`, `friendly_name`
- connectivity: `wifi_connected`, `ip_address`, `mqtt_connected`, `mqtt_state_text`
- install geometry: `room_id`, `sensor_role`, `pose_x_cm`, `pose_y_cm`, `heading_deg`
- live behavior: `presence`, `people_estimate`, `activity_score`, `room_peer_nodes`
- release readiness: `firmware_sync`, `udp_discovery`, and peer firmware versions

## Command endpoint

Example:

```powershell
Invoke-RestMethod 'http://device-host-or-ip/api/command' -Method Post -ContentType 'text/plain' -Body 'status'
```

`wifi_scan` returns a dedicated `wifi_scan_results` payload with `count` and `networks` instead of the normal snapshot body:

```powershell
Invoke-RestMethod 'http://device-host-or-ip/api/command' -Method Post -ContentType 'text/plain' -Body 'wifi_scan'
```

Provisioning example:

```powershell
Invoke-RestMethod 'http://device-host-or-ip/api/command' -Method Post -ContentType 'text/plain' -Body 'ha_config:MyWiFi|secret|mqtt.example.net|1883|espwave|secret|hall-node-01|Hall Node 01'
```

Room placement example:

```powershell
Invoke-RestMethod 'http://device-host-or-ip/api/command' -Method Post -ContentType 'text/plain' -Body 'ha_room_config:conference-a|fixed|230|110|180|620|410'
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

Command families by operator intent:

| Intent | Commands |
| --- | --- |
| Basic health | `ping`, `status`, `ha_status` |
| Provisioning | `ha_config:...`, `ha_ws_config:...`, `ha_mqtt_endpoint:...` |
| Placement and collaboration | `ha_room_config:...`, `ha_room_pose_publish:...` |
| Tuning and diagnostics | `tuning_config:...`, `wifi_scan`, `runtime_benchmark`, `energy`, `radar:<text>` |
| BLE identity | `ble_tag_config:...`, `ble_tag_clear:...` |
| Firmware lifecycle | `firmware_sync`, `firmware_update:<version>` |

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
- Prefer checking `build_target` before OTA so the device is matched to the correct board-specific GitHub release asset.
