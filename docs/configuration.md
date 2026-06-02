# Configuration

This page covers the configuration surfaces that matter in practice: board wiring, Wi-Fi and MQTT provisioning, room geometry, tuning, and BLE tag labeling.

## Configuration summary

| Area | Surface | Stored persistently | Typical operator path |
| --- | --- | --- | --- |
| Board wiring | `board_user_config.h` or `build_flags` | Build-time only | Installer or maintainer |
| Wi-Fi and MQTT | Dashboard or `ha_config:` | Yes | Installer or operator |
| Room placement | Dashboard or `ha_room_config:` | Yes | Installer |
| Tuning | Dashboard or `tuning_config:` | Yes | Installer or advanced operator |
| BLE tags | Dashboard or `ble_tag_config:` | Yes | Operator |

## Board wiring overrides

Board defaults live in `include/board_profile.h`. Local overrides belong in `include/board_user_config.h`, created from `include/board_user_config.example.h`.

Typical override surface:

- Board identity strings.
- USB and radar UART baud rates.
- Radar serial port index.
- Radar RX/TX pins.
- Presence GPIO pin and mode.
- Optional RGB status LED pin and count.

Example local override:

```cpp
#define ESPWAVERIDER_RADAR_RX_PIN 7
#define ESPWAVERIDER_RADAR_TX_PIN 8
#define ESPWAVERIDER_RADAR_PRESENCE_PIN 9
#define ESPWAVERIDER_RADAR_PRESENCE_PIN_MODE INPUT_PULLUP
```

You can also pass overrides through `build_flags` in `platformio.ini` when you want environment-specific wiring without a local header.

Use local header overrides for one-off benches and site-specific boards. Use `build_flags` only when the override is intentional for a named environment.

## Wi-Fi and MQTT provisioning

The dashboard and command surface both support runtime provisioning.

Provisioning command format:

```text
ha_config:<ssid>|<password>|<mqtt_host>|<mqtt_port>|<mqtt_user>|<mqtt_password>|<node_id>|<friendly_name>
```

Notes:

- MQTT can run over raw TCP or WebSockets.
- Hostnames beginning with `ws://`, `wss://`, `http://`, or `https://` are treated as WebSocket endpoints.
- The device stores settings in persistent preferences and reconnects automatically after changes.

Example:

```text
ha_config:MyWiFi|correct-horse-battery-staple|mqtt.example.net|1883|espwave|secret|hall-node-01|Hall Node 01
```

Recommended practice:

- Keep `node_id` stable over the life of the install.
- Use a friendly name that maps to the room or mounting position.
- Validate the resulting `mqtt_connected` and identity fields in `/api/snapshot`.

## Room placement

Room placement is stored in centimeters and degrees:

- `pose_x_cm`
- `pose_y_cm`
- `heading_deg`
- `room_width_cm`
- `room_height_cm`

Room config command format:

```text
ha_room_config:<room_id>|<sensor_role>|<pose_x_cm>|<pose_y_cm>|<heading_deg>|<room_width_cm>|<room_height_cm>
```

Remote room pose publish format:

```text
ha_room_pose_publish:<node_id>|<room_id>|<sensor_role>|<pose_x_cm>|<pose_y_cm>|<heading_deg>|<room_width_cm>|<room_height_cm>
```

Example:

```text
ha_room_config:conference-a|fixed|230|110|180|620|410
```

## Radar tuning

Tuning command format:

```text
tuning_config:<max_range_cm>|<min_gate_energy>|<sensitivity_pct>|<presence_hold_ms>|<min_active_gates>|<min_activity_score>|<led_enabled>|<led_brightness>
```

This controls detection range, gate energy thresholding, presence hold debounce, activity scoring, and LED behavior.

The firmware also applies a stability filter before it reports filtered presence: radar candidates must remain fresh and sustain for multiple polling samples before `detection_candidate` becomes true, then they must miss for a sustained window before the qualified candidate releases. Near-field gate 0/1 clutter is suppressed unless the reported target distance has enough supporting gate energy behind it, which lets real occupants qualify while idle self-noise stays filtered. Diagnostics expose `raw_detection_candidate`, `presence_candidate_hits`, `presence_candidate_misses`, and radar frame `age_ms` so installers can distinguish real sustained occupancy from stale frames or intermittent mmWave noise.

Example:

```text
tuning_config:600|25|80|12000|2|45|1|32
```

## BLE tags

BLE tag commands:

```text
ble_tag_config:<slot>|<label>|<ble_mac>|<min_rssi>
ble_tag_clear:<slot>
```

Use these to associate known BLE devices with labeled occupants or assets.

Example:

```text
ble_tag_config:2|Jerrett Phone|AA:BB:CC:DD:EE:FF|-72
```

Current implementation note:

- C++ supports live BLE observation end to end.
- Rust persists BLE tag configuration, but live scanning remains gated off by default for stability.
