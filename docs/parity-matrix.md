# Parity Matrix

This matrix tracks where the Rust ESP32-S3 bare-metal port currently lands relative to the C++ reference firmware.

Status legend:

- `Full`: implemented and validated for the listed surface.
- `Partial`: implemented, but behavior is asymmetric, stubbed, flaky, or not fully validated.
- `Missing`: not implemented yet.

Current live snapshot on 2026-05-30:

- Rust node `10.0.107.148`: `mqtt_connected=true`, `udp_discovery.peer_count=1`, `room_peers=1`
- C++ node `10.0.107.149`: `mqtt_connected=true`, `udp_discovery.peer_count=1`, `room_peers=1`

That means room-summary fusion and UDP discovery are both stable in both directions on the current default Rust build. The default Rust image is now a production-safe baseline for Wi-Fi, MQTT, UDP discovery, hosted UI, and room collaboration. The remaining parity gaps are isolated to opt-in feature slices: HTTPS OTA apply behind `https-ota` and live BLE observation behind `ble-scan`.

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
| `wifi_scan` | Emits scan results | Returns `wifi_scan_results` JSON with live AP scan data | Full | Live HTTP command on Rust node | Payload now mirrors the C++ scan result shape over the HTTP command surface |
| `ha_room_pose_publish:` | Publishes pose command/event | Publishes MQTT pose command/event | Full | Live HTTP command to Rust updated C++ snapshot pose | Rust now mirrors the C++ MQTT pose-command behavior |
| `ble_tag_config:` | Configures BLE slots | Persists BLE tag slots and exposes them in snapshot state | Full | Live HTTP command on Rust node | Slot config, address normalization, RSSI clamp/default, and persistence now work |
| `ble_tag_clear:` | Clears BLE slots | Clears persisted BLE tag slots and removes them from snapshot state | Full | Live HTTP command on Rust node | Clearing a slot now removes it from the persisted snapshot surface |
| `firmware_sync` | Queues and applies OTA-safe release sync | Queues sync intent, resolves board-specific GitHub release asset URL, and on the default build reports `firmware_https_disabled` for HTTPS assets | Partial | Live HTTP command + firmware compile validation + failed live `https-ota` boot attempt | Rust default builds do not apply OTA; the opt-in `https-ota` feature compiles but still failed to rejoin the LAN on COM17 during live validation |
| `firmware_update:<version>` | Starts targeted OTA update | Records request, resolves board-specific GitHub release asset URL, and on the default build stops with explicit HTTPS-disabled status | Partial | Live HTTP command + firmware compile validation + failed live `https-ota` boot attempt | Command now resolves release-target metadata and fails explicitly on the default build instead of stalling or silently pretending to apply |
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
| Boot diagnostics | Implicit reset/reboot behavior | `boot_count` and `last_reset_reason` fields | Full | Host snapshot tests + live HTTP polling | Rust now exposes persisted boot count and decoded reset reason for stability triage |
| Runtime benchmark snapshot | Full nested benchmark object | Full nested benchmark object | Full | Snapshot contract tests + live command | Shared schema validated |
| Firmware sync snapshot | Full status + OTA lifecycle | Full status object shape plus resolved GitHub asset URL and explicit HTTPS-disabled failure on default builds | Partial | Live HTTP command + firmware compile validation | Shape is present and informative, but true OTA apply parity still requires fixing the live `https-ota` runtime regression |
| UDP discovery snapshot | Live peer announcements and peer list | Live peer announcements and peer list | Full | 60s live hardware burn-in | Both nodes now retain one UDP peer through the validation window |
| Room peer snapshot | MQTT-backed room collaboration peers | MQTT-backed room collaboration peers | Full | 18-sample live burn-in on both nodes | Both nodes held `room_peers=1` across the burn-in window |
| Latest energy frame | Full payload/gates | Full payload/gates | Full | Existing parity runtime tests | Host tests validate metrics contract |
| Latest text frame | Full text frame surface | Full text frame surface | Full | Snapshot contract/code parity | Same schema surface |
| Latest generic frame | Full generic frame surface | Full generic frame surface | Full | Existing parity runtime tests | Host tests validate parse contract |
| BLE beacon snapshot | Real BLE sightings | Default build reports empty list / zero count; opt-in scanner path compiles but is not yet stable on hardware | Partial | Live snapshot/code read + feature compile validation | Rust no longer lacks the code path entirely, but live BLE observation is not production-safe |
| BLE tagged people snapshot | Real BLE tag slots and live presence | Configured BLE tag slots are exposed; live presence remains absent on the default build | Partial | Live command + snapshot validation | Rust reports configured tags, but does not yet populate live presence/RSSI from a production-safe BLE scan path |
| Status LED diagnostics | Color + debug phase | Color in snapshot, debug JSON available | Full | Live snapshot + code path | Rust debug JSON is present |

## Behavioral / Integration Parity

| Area | C++ Reference | Rust Port | Status | Validation | Notes |
| --- | --- | --- | --- | --- | --- |
| AP bring-up and hosted UI | Active | Active | Full | Live board access | Rust board serves the hosted UI and snapshot API |
| STA join and DHCP | Active | Active | Full | Live hardware validation | Regressed then restored by waiting for `stack.wait_config_up()` before MQTT TCP |
| MQTT over TCP | Active | Active | Full | Live hardware validation | Manual Rust MQTT transport is working for current broker setup |
| MQTT over websockets | Active in C++ config surface | Config stored, transport not supported | Missing | Code read | Rust reports unsupported websocket transport |
| Room summary publish | Active | Active | Full | Live C++ sees Rust | Rust publishes summaries that C++ consumes |
| Room summary subscribe | Active | Active | Full | 18-sample live hardware burn-in + host tests | Payload parsing is covered and both nodes held room summaries throughout the burn-in window |
| UDP discovery announce/receive | Active | Active | Full | 60s live hardware burn-in | Removing room-summary payloads from the UDP discovery send path restored symmetric peer retention |
| NVS persistence | Active | Active | Full | Live persistence validation | Same namespace and key set |
| Firmware sync OTA apply | Active GitHub release download/apply | Default build resolves board-specific GitHub release target and stops with explicit HTTPS-disabled status | Partial | Live HTTP command + firmware compile validation | The opt-in `https-ota` build still failed to rejoin the LAN on COM17 during live validation, so OTA parity remains a live runtime blocker rather than a documentation-only gap |
| BLE observation pipeline | Active | Scanner implementation present, feature-gated behind `ble-scan` and disabled in the stable default build | Partial | Live flash/polling + code read + feature compile validation | Any active BLE scanner task knocked the Rust node off the LAN on current USB/power hardware; default builds stay reachable and report `ble_beacon_count=0` |

## Validation Matrix

| Validation Slice | C++ Reference | Rust Port | Status | Evidence |
| --- | --- | --- | --- | --- |
| Snapshot schema contract | Manual JSON output | Shared host parser/tests | Full | `snapshot_contract.rs` passes on host target |
| Radar metrics parity | Runtime benchmark + fixture behavior | Shared core tests | Full | `parity_runtime.rs` passes on host target |
| MQTT room summary payload contract | Implicit in live runtime | Explicit core tests | Full | `mqtt_room_summary_contract.rs` passes |
| MQTT room summary parser benchmark | Not separately isolated | Isolated in core benchmark | Full | `mqtt_room_summary_publish_parse` benchmark runs at ~212.61-221.03 ns on `x86_64-pc-windows-msvc` |
| Firmware compile validation | PlatformIO | Default and optional-feature Rust builds compile | Full | Rust default, `https-ota`, `ble-scan`, and combined feature builds compile; only the default build is currently live-validated as stable |
| Live Rust HTTP snapshot | N/A | Active | Full | `10.0.107.148/api/snapshot` returns good data |
| Live Wi-Fi scan command | Emits `wifi_scan_results` payload | Emits `wifi_scan_results` payload | Full | Live HTTP command on `10.0.107.148` returned 8 APs and the board remained connected |
| Live multi-node room fusion | Active | Stable in both directions | Full | 18-sample burn-in showed `room_peers=1` on both nodes throughout |
| Live multi-node UDP discovery | Active | Stable in both directions | Full | C++ held `udp_discovery.peer_count=1` across the follow-up window after the Rust UDP transport fix |
| Live BLE tag config persistence | Active | Active for configured slot state | Full | Live `ble_tag_config` and `ble_tag_clear` on `10.0.107.148` persisted slot 2, exposed it in snapshot, and removed it again on clear |

## Highest Priority Gaps

1. Fix the Rust `https-ota` live-runtime regression so feature-enabled builds can rejoin the LAN and move `firmware_sync` / `firmware_update` toward true OTA apply parity.
2. Stabilize concurrent Wi-Fi + BLE scanning on the Rust board so the `ble-scan` feature can be enabled without dropping the node off the LAN.
3. Decide release policy for Rust artifacts so operators can distinguish the stable default build from experimental feature-enabled builds.

## Notes

- Host-side `espwaverider-core` tests and Criterion benches must be run against the Windows host target because the workspace default target is Xtensa.
- The stable Rust default build now excludes both `https-ota` and `ble-scan`; on COM17 it restored `10.0.107.148` with `wifi_connected=true`, `mqtt_connected=true`, and `udp_discovery.peer_count=1`.
- `firmware_update:v1.0.0` on the default Rust build resolves the GitHub release asset URL and then reports `firmware_https_disabled`, which is the intended operator-visible contract while `https-ota` remains unstable on hardware.
- Optional Rust feature builds still compile in all combinations tested: default, `--features https-ota`, `--features ble-scan`, and `--features https-ota,ble-scan`.
- A live flash of the `https-ota` build on COM17 still failed to rejoin the LAN, and the C++ peer dropped to `peer_count=0` until the stable default Rust image was restored.
- BLE scanner integration still compiles, links, and flashes, but every live variant tested so far that actively starts the scanner task causes the Rust node to stop answering on `10.0.107.148`; disabling the feature restores MQTT and UDP discovery reachability.
- This matrix is intentionally behavior-first. A row is not `Full` unless the surface is both implemented and validated for its current scope.