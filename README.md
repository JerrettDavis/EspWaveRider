# EspWaveRider

Portable ESP32 mmWave presence and telemetry firmware with a hosted diagnostics UI, BLE observation, room fusion, and Home Assistant/MQTT integration.

The repo is structured so hardware wiring can be remapped without editing application logic. The normal workflow is: choose a PlatformIO environment, optionally override a few board macros, then build and flash.

## Documentation

- The DocFX source lives in `docs/`.
- Build it locally with `dotnet tool update --global docfx` followed by `docfx docs/docfx.json`.
- The generated site is emitted to `docs/_site`.
- `.github/workflows/docs.yml` publishes the site to GitHub Pages from `main`.

## What it does

- Reads HLK-LD2420 radar data over UART plus presence GPIO
- Tracks local presence, activity, and room-level fused state
- Publishes device and observation data to MQTT / Home Assistant
- Hosts a built-in device UI for setup, diagnostics, room editing, and sensor inspection
- Reports firmware version/build metadata in both the device UI and diagnostics payloads
- Detects peer firmware mismatches and exposes the highest visible peer release for safe OTA sync
- Supports board-specific wiring through a small HAL layer

## Supported environments

- `esp32-s3-devkitm-1`: validated primary target
- `esp32dev-uart1`: generic ESP32 example target
- `heltec-wifi-lora-32-v3`: Heltec WiFi LoRa 32 V3 target
- `heltec-wifi-lora-32-v4`: Heltec WiFi LoRa 32 V4-compatible target

## Hardware abstraction

Board defaults live in `include/board_profile.h`.

You can override them in either of two ways:

1. Create `include/board_user_config.h` from `include/board_user_config.example.h`
2. Pass override macros through `build_flags` in `platformio.ini`

The override surface includes:

- board metadata
- USB baud rate
- radar UART baud rate and UART index
- radar RX/TX pins
- radar presence GPIO pin and mode
- optional RGB status LED pin/count

Example local override:

```cpp
#define ESPWAVERIDER_RADAR_RX_PIN 7
#define ESPWAVERIDER_RADAR_TX_PIN 8
#define ESPWAVERIDER_RADAR_PRESENCE_PIN 9
#define ESPWAVERIDER_RADAR_PRESENCE_PIN_MODE INPUT_PULLUP
```

## Heltec V4 note

There is no native PlatformIO `V4` board ID available in this environment. The `heltec-wifi-lora-32-v4` environment reuses PlatformIO's `heltec_wifi_lora_32_V3` definition and overrides flash sizing for V4 hardware.

The current V4-compatible default LD2420 wiring is intentionally simple:

- radar RX: `GPIO2`
- radar TX: `GPIO3`
- radar presence: `GPIO4`

Those are only defaults and can be remapped locally.

## Build

Local Windows PlatformIO path used in this workspace:

```powershell
C:\Users\jd\.platformio\penv\Scripts\platformio.exe
```

Build the primary target:

```powershell
C:\Users\jd\.platformio\penv\Scripts\platformio.exe run --environment esp32-s3-devkitm-1
```

Build the generic ESP32 target:

```powershell
C:\Users\jd\.platformio\penv\Scripts\platformio.exe run --environment esp32dev-uart1
```

Build the Heltec V3 target:

```powershell
C:\Users\jd\.platformio\penv\Scripts\platformio.exe run --environment heltec-wifi-lora-32-v3
```

Build the Heltec V4-compatible target:

```powershell
C:\Users\jd\.platformio\penv\Scripts\platformio.exe run --environment heltec-wifi-lora-32-v4
```

Upload the primary target:

```powershell
C:\Users\jd\.platformio\penv\Scripts\platformio.exe run --environment esp32-s3-devkitm-1 --target upload --upload-port COM17
```

## Testing

- Firmware build validation is handled through PlatformIO environments.
- `npm run test:unit` runs fast Python unit tests for the firmware metadata and visualizer embedding helpers.
- `npm run test:integration` runs Python integration tests that verify HTML embedding/header generation behavior.
- Browser-based `e2e/` Playwright coverage is split into two lanes:
	- `npm run test:e2e:offline` exercises the hosted dashboard against mocked device snapshots and is safe to run on GitHub-hosted runners.
	- `npm run test:e2e:live` exercises real multi-node device behavior and is intended for hardware-backed or self-hosted runners.
	- `npm run test:e2e:local` is the local alias for the live device suite when developing against nodes on the LAN.
- GitHub Actions CI builds the supported firmware environments on every push and pull request, runs Python unit tests, runs Python integration tests, and runs the offline dashboard Playwright suite on `ubuntu-latest`.
- The release pipeline also runs the Python unit tests, Python integration tests, and offline dashboard Playwright suite before semantic release publishes versioned artifacts.
- The manual `hardware-e2e` workflow is available for self-hosted runners that can reach real devices.

## Releases

- Releases are created automatically from commits on `main` using conventional commit messages.
- Versioning rules are semantic: `feat:` produces a minor release, `fix:` and `perf:` produce patch releases, and `BREAKING CHANGE:` or `!` produces a major release.
- A successful release run creates a GitHub release tag like `v1.2.3`, updates `CHANGELOG.md`, and publishes board-specific firmware binaries for every supported PlatformIO environment.
- Published release assets use the form `EspWaveRider-<version>-<environment>.bin` plus a matching `.sha256` checksum file.

## Versioning and firmware sync

- Firmware builds stamp version, build target, and git SHA into the device snapshot and Home Assistant diagnostics.
- Tagged GitHub releases are the OTA source of truth. Nodes do not relay binaries to each other yet.
- A node can sync itself to the highest tagged peer release that matches its board target by downloading the matching GitHub release asset.
- OTA sync uses pinned HTTPS trust anchors for GitHub, resolves the expected firmware digest from the matching release metadata, and verifies the downloaded binary stream with SHA-256 before activating the update.
- This protects the network update path against local MITM tampering, but true device-level anti-malware guarantees still require secure boot, flash encryption, and an offline signing story for release artifacts.
- If peers are on local or unknown builds, the dashboard keeps the sync action disabled and reports why no safe candidate is available.

Manual command examples:

```text
firmware_sync
firmware_update:v1.0.0
```

Example commit subjects:

- `feat: add room calibration enrollment workflow`
- `fix: clamp presence decay to avoid UI flicker`
- `feat!: rename mqtt observation schema`

## Repository layout

- `src/main.cpp`: main firmware runtime
- `src/visualizer/index.html`: hosted device UI source
- `include/board_profile.h`: board defaults and HAL mapping
- `include/board_user_config.example.h`: local override template
- `platformio.ini`: PlatformIO environments
- `scripts/embed_visualizer.py`: embeds the hosted UI into firmware
- `e2e/`: hardware-backed Playwright coverage

## Publishing notes

- The project name is still provisional, but `EspWaveRider` is the current working name.
- Local secrets and local board overrides are excluded via `.gitignore`.
- Review broker addresses, screenshots, and any environment-specific examples before making the repo public.
- When the repository is ready, the intended remote is `https://github.com/JerrettDavis/EspWaveRider.git`.