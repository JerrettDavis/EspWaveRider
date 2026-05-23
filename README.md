# EspWaveRider

Working title for a portable ESP32 mmWave presence and telemetry firmware stack with Home Assistant, BLE observation, room fusion, and a hosted diagnostics UI.
The firmware is now organized so hardware mapping is configurable without touching application logic. The intended workflow is: pull the repo, tweak a few pin-related macros, then build and flash.

## Current targets

- `esp32-s3-devkitm-1`: current validated primary target
- `esp32dev-uart1`: generic ESP32 example target
- `heltec-wifi-lora-32-v3`: Heltec WiFi LoRa 32 V3 target
# EspWaveRider

Portable ESP32 mmWave presence and telemetry firmware with a hosted diagnostics UI, BLE observation, room fusion, and Home Assistant/MQTT integration.

The repo is structured so hardware wiring can be remapped without editing application logic. The normal workflow is: choose a PlatformIO environment, optionally override a few board macros, then build and flash.

## What it does

- Reads HLK-LD2420 radar data over UART plus presence GPIO
- Tracks local presence, activity, and room-level fused state
- Publishes device and observation data to MQTT / Home Assistant
- Hosts a built-in device UI for setup, diagnostics, room editing, and sensor inspection
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
- Browser-based `e2e/` Playwright coverage exists for live device scenarios, but those tests are hardware-dependent and are not run in CI.
- GitHub Actions CI builds the supported firmware environments on every push and pull request.

## Releases

- Releases are created automatically from commits on `main` using conventional commit messages.
- Versioning rules are semantic: `feat:` produces a minor release, `fix:` and `perf:` produce patch releases, and `BREAKING CHANGE:` or `!` produces a major release.
- A successful release run creates a GitHub release tag like `v1.2.3`, updates `CHANGELOG.md`, and publishes board-specific firmware binaries for every supported PlatformIO environment.
- Published release assets use the form `EspWaveRider-<version>-<environment>.bin` plus a matching `.sha256` checksum file.

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