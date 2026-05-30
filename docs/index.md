# EspWaveRider

EspWaveRider is ESP32 firmware for mmWave presence sensing, diagnostics, Home Assistant / MQTT publishing, BLE observation, peer-aware room fusion, and hosted device management.

The documentation site is organized around the normal device lifecycle:

- Install the firmware and toolchain.
- Get a node online and validate the first snapshot.
- Configure board-specific wiring and runtime settings.
- Operate the hosted dashboard, room editor, and firmware sync flow.
- Validate changes locally, compare C++ vs Rust behavior, and understand the release pipeline.

## Core capabilities

- Reads HLK-LD2420 UART frames plus presence GPIO.
- Publishes Home Assistant discovery, state, diagnostics, and observation topics.
- Hosts an embedded browser dashboard for provisioning, diagnostics, tuning, room editing, and firmware sync.
- Detects peers on the network, surfaces version mismatches, and links directly to peer dashboards.
- Builds board-specific release binaries from conventional commits.

## Quick links

- [Install the firmware](install.md)
- [Getting started](getting-started.md)
- [Configure wiring and runtime settings](configuration.md)
- [Operate the dashboard and OTA workflow](operations.md)
- [Inspect the HTTP and command surface](api.md)
- [Review the current C++ vs Rust benchmark and size comparison](benchmarks-and-comparison.md)
- [Check the validated feature parity matrix](parity-matrix.md)
- [Run tests and understand releases](testing-and-release.md)
- [Review the release process and release quality bar](release-guide.md)

## Recommended reading order

1. [Install](install.md)
2. [Getting started](getting-started.md)
3. [Configuration](configuration.md)
4. [Operations](operations.md)
5. [Benchmarks and comparison](benchmarks-and-comparison.md)
6. [Parity matrix](parity-matrix.md)
7. [Testing and releases](testing-and-release.md)
8. [Release guide](release-guide.md)

## Repository structure

- `src/main.cpp`: main firmware runtime and command handling.
- `src/visualizer/index.html`: hosted dashboard source.
- `src/device_web.cpp`: device HTTP and WebSocket endpoints.
- `include/board_profile.h`: board defaults.
- `include/board_user_config.example.h`: local override template.
- `scripts/embed_visualizer.py`: embeds the dashboard into firmware at build time.

## Production-readiness snapshot

| Area | Current position |
| --- | --- |
| Shipping firmware baseline | C++ |
| Board-specific GitHub release assets | Implemented |
| Hosted dashboard, snapshot API, room collaboration | Production-oriented and documented |
| Rust parity work | Strong on core runtime and contracts; stable default build, but not full-feature-equivalent yet |

The Rust port is close enough to compare seriously and now has a stable default build for Wi-Fi, MQTT, UDP discovery, and room collaboration. It is still not something this repo should market as a production-equivalent replacement for the C++ firmware because OTA apply and live BLE observation remain opt-in, not yet production-safe feature slices.
