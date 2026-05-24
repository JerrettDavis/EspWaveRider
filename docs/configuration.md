# Configuration

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

## Radar tuning

Tuning command format:

```text
tuning_config:<max_range_cm>|<min_gate_energy>|<sensitivity_pct>|<presence_hold_ms>|<min_active_gates>|<min_activity_score>|<led_enabled>|<led_brightness>
```

This controls detection range, gate energy thresholding, presence hold debounce, activity scoring, and LED behavior.

## BLE tags

BLE tag commands:

```text
ble_tag_config:<slot>|<label>|<ble_mac>|<min_rssi>
ble_tag_clear:<slot>
```

Use these to associate known BLE devices with labeled occupants or assets.
