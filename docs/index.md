# EspWaveRider

EspWaveRider is ESP32 firmware for mmWave presence sensing, diagnostics, Home Assistant / MQTT publishing, BLE observation, peer-aware room fusion, and hosted device management.

The documentation site is organized around the normal device lifecycle:

- Install the firmware and toolchain.
- Configure board-specific wiring and runtime settings.
- Operate the hosted dashboard, room editor, and firmware sync flow.
- Validate changes locally and understand the release pipeline.

## Core capabilities

- Reads HLK-LD2420 UART frames plus presence GPIO.
- Publishes Home Assistant discovery, state, diagnostics, and observation topics.
- Hosts an embedded browser dashboard for provisioning, diagnostics, tuning, room editing, and firmware sync.
- Detects peers on the network, surfaces version mismatches, and links directly to peer dashboards.
- Builds board-specific release binaries from conventional commits.

## Quick links

- [Install the firmware](install.md)
- [Configure wiring and runtime settings](configuration.md)
- [Operate the dashboard and OTA workflow](operations.md)
- [Inspect the HTTP and command surface](api.md)
- [Run tests and understand releases](testing-and-release.md)

## Repository structure

- `src/main.cpp`: main firmware runtime and command handling.
- `src/visualizer/index.html`: hosted dashboard source.
- `src/device_web.cpp`: device HTTP and WebSocket endpoints.
- `include/board_profile.h`: board defaults.
- `include/board_user_config.example.h`: local override template.
- `scripts/embed_visualizer.py`: embeds the dashboard into firmware at build time.
