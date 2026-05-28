# EspWaveRider

Portable ESP32 mmWave presence and telemetry firmware with a hosted diagnostics UI, BLE observation, room fusion, and Home Assistant/MQTT integration.

EspWaveRider ships a production-oriented C++ firmware today and a bare-metal Rust ESP32-S3 port that is being driven against the same external contracts with parity tests, live burn-in, and A/B benchmarks.

## Quick start

1. Install dependencies with `npm ci`.
2. Build or flash the C++ firmware for your board.
3. Open the hosted dashboard or `GET /api/snapshot`.
4. Provision Wi-Fi, MQTT, node identity, room placement, and tuning.
5. Verify peer discovery, MQTT, and release-target metadata before deploying more nodes.

Start here for the full path:

- `docs/install.md`: prerequisites and build/flash commands
- `docs/getting-started.md`: first boot, provisioning, validation, and common operator checks
- `docs/configuration.md`: wiring, provisioning, room, BLE, and tuning configuration
- `docs/operations.md`: dashboard, peer discovery, firmware sync, and troubleshooting
- `docs/parity-matrix.md`: validated C++ vs Rust feature status
- `docs/benchmarks-and-comparison.md`: current C++ vs Rust benchmark and size comparison
- `docs/release-guide.md`: release process, asset naming, and release quality bar

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

## Feature matrix

| Area | C++ firmware | Rust ESP32-S3 port | Notes |
| --- | --- | --- | --- |
| Hosted dashboard and snapshot API | Yes | Yes | Rust serves the same device UI and snapshot surface |
| Wi-Fi station runtime | Yes | Yes | Rust is currently validated in station-only configured runtime |
| SoftAP provisioning fallback | Yes | Yes | Rust keeps SoftAP for the unconfigured path |
| MQTT over TCP | Yes | Yes | Validated live on the Rust board |
| MQTT over WebSockets | Yes | Config only | Rust stores endpoint config but does not run WS MQTT transport yet |
| UDP peer discovery | Yes | Yes | Stable in both directions in the latest burn-in window |
| Room-summary collaboration | Yes | Yes | Stable in both directions in the latest burn-in window |
| OTA apply | Yes | Partial | Rust resolves targets and state but does not apply firmware yet |
| BLE observation | Yes | Partial | Rust scanner code exists but is runtime-gated off for stability |

For row-by-row status and evidence, see `docs/parity-matrix.md`.

## Supported environments

| PlatformIO environment | Published release target | Status |
| --- | --- | --- |
| `esp32-s3-devkitm-1` | `lonely-esp32-s3-devkitm-1` | Validated primary target |
| `esp32dev-uart1` | `esp32-generic-uart1` | Generic ESP32 example target |
| `heltec-wifi-lora-32-v3` | `heltec-wifi-lora-32-v3` | Supported |
| `heltec-wifi-lora-32-v4` | `heltec-wifi-lora-32-v4-compatible` | V4-compatible profile |

Release assets intentionally use board-specific target names instead of raw PlatformIO environment IDs.

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

Run the automated C++ vs Rust device benchmark collector:

```powershell
& '.\scripts\collect-ab-benchmarks.ps1'
```

## C++ vs Rust summary

Current validated position:

- C++ remains the production baseline across the full shipping feature set.
- Rust has strong parity on snapshot, command parsing, room collaboration, UDP discovery, MQTT over TCP, and the hosted diagnostics surface.
- Rust still has two meaningful gaps before it can claim production equivalence: OTA apply and stable BLE scanning.
- On the latest A/B device benchmark run, Rust won 5 of 6 measured slices and C++ held a slight lead only on `detection_candidate_fixture`.
- Current size snapshot on the primary board is approximately `1063.5 KB` for the Rust ELF and `1359.97 KB` for the C++ BIN.

See `docs/benchmarks-and-comparison.md` for the current benchmark table, size report, and methodology.

## Testing

- Firmware build validation is handled through PlatformIO environments.
- `scripts/collect-ab-benchmarks.ps1` uploads the C++ image, runs `runtime_benchmark` over HTTP, builds and flashes the Rust benchmark image, captures the Rust serial benchmark lines, writes `artifacts/ab-benchmark-latest.json`, and restores the C++ image on the test board.
- `npm run test:unit` runs fast Python unit tests for the firmware metadata and visualizer embedding helpers.
- `npm run test:integration` runs Python integration tests that verify HTML embedding/header generation behavior.
- Browser-based `e2e/` Playwright coverage is split into two lanes:
	- `npm run test:e2e:offline` exercises the hosted dashboard against mocked device snapshots and is safe to run on GitHub-hosted runners.
	- `npm run test:e2e:live` exercises real multi-node device behavior and is intended for hardware-backed or self-hosted runners.
	- `npm run test:e2e:local` is the local alias for the live device suite when developing against nodes on the LAN.
- GitHub Actions CI builds the supported firmware environments on every push and pull request, runs Python unit tests, runs Python integration tests, and runs the offline dashboard Playwright suite on `ubuntu-latest`.
- The release pipeline also runs the Python unit tests, Python integration tests, and offline dashboard Playwright suite before semantic release publishes versioned artifacts.
- The manual `hardware-e2e` workflow is available for self-hosted runners that can reach real devices.

The short version is:

- `npm run test:unit`: Python unit coverage for embed/build metadata helpers
- `npm run test:integration`: Python integration coverage for hosted UI embedding
- `npm run test:e2e:offline`: hosted-safe UI workflow coverage
- `npm run docs:build`: local DocFX validation for docs and internal links
- `npm run release:preview`: semantic-release dry-run for next-version and release-note preview
- `scripts/collect-ab-benchmarks.ps1`: cross-language benchmark collection on real hardware
- `docs/parity-matrix.md`: current validated feature-status source of truth

CI also uploads a rendered `docs/_site` preview artifact for documentation changes so reviewers can inspect the generated site before merge.

## Releases

- Releases are created automatically from commits on `main` using conventional commit messages.
- Versioning rules are semantic: `feat:` produces a minor release, `fix:` and `perf:` produce patch releases, and `BREAKING CHANGE:` or `!` produces a major release.
- A successful release run creates a GitHub release tag like `v1.2.3`, updates `CHANGELOG.md`, and publishes board-specific firmware binaries for every supported PlatformIO environment.
- Published release assets use explicit board targets, not raw PlatformIO environment IDs. Current names are:
- `EspWaveRider-<version>-lonely-esp32-s3-devkitm-1.bin`
- `EspWaveRider-<version>-esp32-generic-uart1.bin`
- `EspWaveRider-<version>-heltec-wifi-lora-32-v3.bin`
- `EspWaveRider-<version>-heltec-wifi-lora-32-v4-compatible.bin`
- Each published binary also includes a matching `.sha256` checksum file.

If you are deploying from GitHub Releases, match the published release target exactly. Do not treat the primary `lonely-esp32-s3-devkitm-1` binary as a generic ESP32-S3 image.

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
runtime_benchmark
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
- `docs/`: operator and release documentation
- `rust/`: bare-metal Rust port workspace and shared parity crates

## Publishing notes

- The project name is still provisional, but `EspWaveRider` is the current working name.
- Local secrets and local board overrides are excluded via `.gitignore`.
- Review broker addresses, screenshots, and any environment-specific examples before making the repo public.
- When the repository is ready, the intended remote is `https://github.com/JerrettDavis/EspWaveRider.git`.