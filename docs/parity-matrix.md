# Parity Matrix

This matrix tracks where the Rust ESP32-S3 bare-metal port currently lands relative to the C++ reference firmware.

Status legend:

- `Full`: implemented and validated for the listed surface.
- `Partial`: implemented, but behavior is asymmetric, stubbed, flaky, or not fully validated.
- `Missing`: not implemented yet.

Current live snapshot on 2026-05-27:

- Rust node `10.0.107.148`: `mqtt_connected=true`, `udp_discovery.peer_count=1`, `room_peers=1`
- C++ node `10.0.107.149`: `mqtt_connected=true`, `udp_discovery.peer_count=1`, `room_peers=1`

That means the latest live sample showed symmetric peer visibility on both MQTT room summaries and UDP discovery. Those rows still remain `Partial` below because peer visibility was intermittent earlier in the same session and needs a longer stable validation window before it can be called `Full`.

## Command Surface

| Surface | C++ Reference | Rust Port | Status | Validation | Notes |
| --- | --- | --- | --- | --- | --- |
| `ping` | Returns pong event | Parsed and handled | Full | Live serial/runtime usage | Basic command path works |
| `status` / `snapshot` | Emits heartbeat/config state | Returns full snapshot JSON | Full | Live HTTP + serial | Rust uses snapshot JSON as the status contract |
| `debug_status` | Emits debug JSON | Emits debug JSON | Full | Live serial, code path present | Useful for detection/status diagnostics |
| `ha_status` | Emits HA config event | Returns snapshot JSON | Full | Live snapshot fields | Contract-level parity is acceptable because snapshot contains HA fields |
| `ha_config:` | Persists Wi-Fi/MQTT/node config | Persists NVS config | Full | Live boot persistence validated | Same persisted namespace and keys |
| `ha_room_config:` | Persists room geometry/role | Persists room geometry/role | Full | Live persistence validated | Included in snapshot parity |
| `ha_ws_config:` | Persists WS settings | Persists WS settings | Full | Command parsing + persistence path | Transport use remains limited by MQTT websocket support |
| `ha_mqtt_endpoint:` | Persists endpoint/transport settings | Persists endpoint/transport settings | Full | Command parsing + live TCP MQTT path | Websocket endpoint config stores correctly |
| `tuning_config:` | Persists tuning and LED values | Persists tuning and LED values | Full | Existing parser/runtime tests | Included in runtime benchmark contract |
| `wifi_scan` | Emits scan results | Parses command but returns snapshot | Partial | Code read only | No scan result parity yet |
| `ha_room_pose_publish:` | Publishes pose command/event | Publishes MQTT pose command/event | Full | Live HTTP command to Rust updated C++ snapshot pose | Rust now mirrors the C++ MQTT pose-command behavior |
| `ble_tag_config:` | Configures BLE slots | Parses command but returns snapshot | Partial | Code read only | BLE stack not implemented in Rust |
| `ble_tag_clear:` | Clears BLE slots | Parses command but returns snapshot | Partial | Code read only | BLE stack not implemented in Rust |
| `firmware_sync` | Queues and applies OTA-safe release sync | Queues/statefully reports sync intent | Partial | Live snapshot validation | Rust exposes sync state but does not download/apply firmware |
| `firmware_update:<version>` | Starts targeted OTA update | Records request then resolves `not_implemented` | Partial | Live state validation | Command is present, behavior is still stubbed |
| `runtime_benchmark` | Runs benchmark and publishes snapshot/event | Runs benchmark and returns snapshot | Full | Live command + host benchmark support | Rust now has host benchmark coverage for MQTT parser slice too |
| `energy` | Forces LD2420 energy mode | Forces LD2420 energy mode | Full | Code path parity | Same operational intent |
| `radar:<payload>` | Raw UART passthrough | Raw UART passthrough | Full | Code path parity | Same operational intent |

## Snapshot And Diagnostics Surface

| Surface | C++ Reference | Rust Port | Status | Validation | Notes |
| --- | --- | --- | --- | --- | --- |
| Core identity/config fields | Manual JSON builder | Shared `DeviceSnapshot` contract | Full | Host snapshot tests + live HTTP | Node, room, MQTT, firmware metadata are present |
| Wi-Fi link fields | Real ESP Wi-Fi state | Real `embassy`/radio state | Full | Live HTTP on `10.0.107.148` | DHCP/IP regression fixed on 2026-05-27 |
| MQTT state fields | `PubSubClient` state/status | Manual MQTT task state/status | Full | Live HTTP + MQTT-connected snapshots | TCP MQTT now connects reliably |
| Firmware metadata | Version/build/git SHA | Version/build/git SHA | Full | Snapshot contract tests | Same top-level fields |
| Runtime benchmark snapshot | Full nested benchmark object | Full nested benchmark object | Full | Snapshot contract tests + live command | Shared schema validated |
| Firmware sync snapshot | Full status + OTA lifecycle | Full status object shape | Partial | Live snapshot + code read | Shape is present, OTA apply path is not |
| UDP discovery snapshot | Live peer announcements and peer list | Live peer announcements and peer list | Partial | Live hardware | Rust currently sees one UDP peer while C++ currently reports zero |
| Room peer snapshot | MQTT-backed room collaboration peers | MQTT-backed room collaboration peers | Partial | Live hardware | Latest live sample was symmetric, but peer visibility was intermittent earlier in the session |
| Latest energy frame | Full payload/gates | Full payload/gates | Full | Existing parity runtime tests | Host tests validate metrics contract |
| Latest text frame | Full text frame surface | Full text frame surface | Full | Snapshot contract/code parity | Same schema surface |
| Latest generic frame | Full generic frame surface | Full generic frame surface | Full | Existing parity runtime tests | Host tests validate parse contract |
| BLE beacon snapshot | Real BLE sightings | Empty list / zero count | Missing | Live snapshot/code read | Rust reports empty placeholders only |
| BLE tagged people snapshot | Real BLE tag slots | Empty list / zero count | Missing | Live snapshot/code read | Rust reports empty placeholders only |
| Status LED diagnostics | Color + debug phase | Color in snapshot, debug JSON available | Full | Live snapshot + code path | Rust debug JSON is present |

## Behavioral / Integration Parity

| Area | C++ Reference | Rust Port | Status | Validation | Notes |
| --- | --- | --- | --- | --- | --- |
| AP bring-up and hosted UI | Active | Active | Full | Live board access | Rust board serves the hosted UI and snapshot API |
| STA join and DHCP | Active | Active | Full | Live hardware validation | Regressed then restored by waiting for `stack.wait_config_up()` before MQTT TCP |
| MQTT over TCP | Active | Active | Full | Live hardware validation | Manual Rust MQTT transport is working for current broker setup |
| MQTT over websockets | Active in C++ config surface | Config stored, transport not supported | Missing | Code read | Rust reports unsupported websocket transport |
| Room summary publish | Active | Active | Full | Live C++ sees Rust | Rust publishes summaries that C++ consumes |
| Room summary subscribe | Active | Active but still being burn-in validated | Partial | Live hardware + new host tests | Payload parsing is covered and the latest live sample was symmetric, but earlier peer visibility flapped |
| UDP discovery announce/receive | Active | Active but still being burn-in validated | Partial | Live hardware | Latest live sample was symmetric, but C++ peer retention flapped earlier in the session |
| NVS persistence | Active | Active | Full | Live persistence validation | Same namespace and key set |
| Firmware sync OTA apply | Active GitHub release download/apply | Stubbed final action | Missing | Code read + live snapshot state | Rust currently stops at `not_implemented` |
| BLE observation pipeline | Active | Not present | Missing | Live snapshot/code read | Major remaining gap |

## Validation Matrix

| Validation Slice | C++ Reference | Rust Port | Status | Evidence |
| --- | --- | --- | --- | --- |
| Snapshot schema contract | Manual JSON output | Shared host parser/tests | Full | `snapshot_contract.rs` passes on host target |
| Radar metrics parity | Runtime benchmark + fixture behavior | Shared core tests | Full | `parity_runtime.rs` passes on host target |
| MQTT room summary payload contract | Implicit in live runtime | Explicit core tests | Full | `mqtt_room_summary_contract.rs` passes |
| MQTT room summary parser benchmark | Not separately isolated | Isolated in core benchmark | Full | `mqtt_room_summary_publish_parse` benchmark runs at ~212.61-221.03 ns on `x86_64-pc-windows-msvc` |
| Firmware compile validation | PlatformIO | `cargo +esp check` | Full | Rust firmware compiles after parser extraction |
| Live Rust HTTP snapshot | N/A | Active | Full | `10.0.107.148/api/snapshot` returns good data |
| Live multi-node room fusion | Active | Restored and currently symmetric | Partial | Latest live snapshots showed both nodes with `room_peers=1`; longer burn-in still needed |
| Live multi-node UDP discovery | Active | Restored and currently symmetric | Partial | Latest live snapshots showed both nodes with `udp_discovery.peer_count=1`; longer burn-in still needed |

## Highest Priority Gaps

1. Burn in symmetric room-summary and UDP peer retention over a longer live window before upgrading those rows from `Partial` to `Full`.
2. Implement actual OTA download/apply behavior behind `firmware_sync` / `firmware_update`.
3. Implement BLE beacon/tag observation and command handling instead of placeholder zero/empty snapshot fields.
4. Implement real `wifi_scan` command behavior in Rust.

## Notes

- Host-side `espwaverider-core` tests and Criterion benches must be run against the Windows host target because the workspace default target is Xtensa.
- This matrix is intentionally behavior-first. A row is not `Full` unless the surface is both implemented and validated for its current scope.