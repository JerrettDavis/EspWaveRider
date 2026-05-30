# Operations

## Hosted dashboard

The device serves the embedded dashboard at `/` and exposes a fast polling plus WebSocket-backed live view.

Operational areas in the UI include:

- Provisioning Wi-Fi and MQTT.
- Reviewing presence, activity, distance, BLE, and diagnostics.
- Editing room geometry and node placement.
- Inspecting peers discovered over UDP and room-summary MQTT traffic.
- Triggering firmware sync when a safe peer release is available.

## Peer discovery and room collaboration

Nodes broadcast discovery metadata including:

- Node ID.
- Friendly name.
- Firmware version.
- Hostname and IP address.
- Wi-Fi signal metadata.

The dashboard uses that information to surface clickable peer links and warn when peers are running mismatched firmware versions.

## Firmware sync and OTA

Firmware sync uses tagged GitHub releases as the source of truth.

Current behavior:

- A node looks for the highest visible peer release that matches its board target.
- The actual binary is downloaded from GitHub Releases, not copied from peers.
- C++ performs the full HTTPS download and apply flow.
- The default Rust build resolves peer release metadata and the board-specific GitHub asset URL, but stops with `firmware_https_disabled` unless it was built with `https-ota`.
- When `https-ota` is enabled and validated, the Rust path is intended to validate release metadata and stream the image into the OTA partition rather than copying from peers.

Manual commands:

```text
firmware_sync
firmware_update:v1.0.0
```

## Troubleshooting

Useful checks when a node is not behaving correctly:

- Confirm Wi-Fi and MQTT status in the dashboard snapshot.
- Verify the device dashboard URL and mDNS hostname.
- Inspect `/api/snapshot` for the stored configuration and live metrics.
- Use serial or `/api/command` to resend configuration commands.
- Confirm peers are on compatible firmware versions before attempting sync.
