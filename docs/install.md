# Install

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

- `esp32-s3-devkitm-1`
- `esp32dev-uart1`
- `heltec-wifi-lora-32-v3`
- `heltec-wifi-lora-32-v4`

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
