# Install

If you want the shortest path from clone to a provisioned node, read [Getting Started](getting-started.md) after this page.

## Prerequisites

You need the following tools to build and work with EspWaveRider:

- PlatformIO CLI or the VS Code PlatformIO extension.
- Python 3.x for helper scripts and Python-based tests.
- Node.js for Playwright and release automation.
- A USB data connection to the target ESP32 board.
- .NET SDK only if you want to build this DocFX site locally.

## Clone and install dependencies

Install JavaScript tooling from the repository root:

```powershell
npm ci
```

If `platformio` is not on `PATH`, use the direct executable path on Windows:

```powershell
C:\Users\jd\.platformio\penv\Scripts\platformio.exe run --environment esp32-s3-devkitm-1
```

Otherwise the standard command works:

```powershell
platformio run --environment esp32-s3-devkitm-1
```

## Supported build targets

| PlatformIO environment | Published release target | Notes |
| --- | --- | --- |
| `esp32-s3-devkitm-1` | `lonely-esp32-s3-devkitm-1` | Primary validated target |
| `esp32dev-uart1` | `esp32-generic-uart1` | Generic ESP32 example target |
| `heltec-wifi-lora-32-v3` | `heltec-wifi-lora-32-v3` | Supported |
| `heltec-wifi-lora-32-v4` | `heltec-wifi-lora-32-v4-compatible` | V4-compatible profile |

## Build

Examples:

```powershell
platformio run --environment esp32-s3-devkitm-1
platformio run --environment esp32dev-uart1
platformio run --environment heltec-wifi-lora-32-v3
platformio run --environment heltec-wifi-lora-32-v4
```

## Upload

Example upload to a specific serial port:

```powershell
platformio run --environment esp32-s3-devkitm-1 --target upload --upload-port COM17
```

## First boot

After flashing, the device can be reached in one of these ways:

- By its hosted dashboard URL when Wi-Fi and mDNS are available.
- By the fallback access point if the device is not yet provisioned.
- Over serial if you need to provision settings with raw commands.

The hosted dashboard is the preferred setup path for Wi-Fi, MQTT, room placement, and tuning.

Minimum acceptance checks after first boot:

- `GET /api/snapshot` returns valid JSON
- the reported firmware version and build target are correct
- Wi-Fi and MQTT state match your expected provisioning state
- the board-specific release target matches the binary you flashed
