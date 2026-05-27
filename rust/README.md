# Rust Port Workspace

This workspace is the start of a clean Rust port of the ESP32 mmWave firmware.

Current goals:

- preserve the existing externally visible board and API contract
- keep core logic host-testable before binding it to ESP32 hardware
- create a safe seam for A/B parity testing against the C++ firmware

Initial scope in this branch:

- typed board profile models matching the current firmware defaults
- typed command family parsing for the existing HTTP command surface
- typed snapshot structures for parity-oriented fixture testing
- LD2420 frame parsing and derived-metrics logic ported from the current C++ firmware
- fixture-driven parity runtime tests plus host/device benchmark scaffolding

Planned next layers:

- LD2420 frame parser and derived metrics in a no-std-friendly crate
- state machine for configuration, room fusion, and transport orchestration
- ESP32-S3 bare-metal app crate using `esp-hal`
- hardware parity tests and perf measurements against the existing C++ image

Embedded bootstrap status:

- `firmware/esp32s3` is a real `no_std` bare-metal app crate targeting `esp-hal`
- `crates/espwaverider-core` now builds as `no_std + alloc`, while snapshot/JSON contract support stays feature-gated for host-side parity tests
- once the toolchain is installed, it becomes the place to bind UART, GPIO, Wi-Fi, BLE, and the hosted device API to the core crates

ESP32-S3 toolchain setup on a fresh machine:

```powershell
& 'C:\Users\jd\.cargo\bin\cargo.exe' install espup
& 'C:\Users\jd\.cargo\bin\espup.exe' install --targets esp32s3 --default-host x86_64-pc-windows-msvc --name esp --export-file 'C:\git\LB-ESP32S3-MMWave-Testing\rust\export-esp.ps1'
```

Embedded build once the Xtensa toolchain is installed:

```powershell
Push-Location 'C:\git\LB-ESP32S3-MMWave-Testing\rust'
& '.\export-esp.ps1'
& 'C:\Users\jd\.cargo\bin\cargo.exe' +esp build --release -p espwaverider-esp32s3
Pop-Location
```

The current release artifact is emitted at `rust/target/xtensa-esp32s3-none-elf/release/espwaverider-esp32s3`.

Local validation from this repository root:

```powershell
& 'C:\Users\jd\.cargo\bin\cargo.exe' test --manifest-path rust/Cargo.toml
```

Host-side parity benchmark from this repository root:

```powershell
& 'C:\Users\jd\.cargo\bin\cargo.exe' bench --manifest-path rust/crates/espwaverider-core/Cargo.toml --bench parity_bench
```

Device-side benchmark build from the Rust workspace root:

```powershell
Push-Location 'C:\git\LB-ESP32S3-MMWave-Testing\rust'
& '.\export-esp.ps1'
& 'C:\Users\jd\.cargo\bin\cargo.exe' +esp build --release -p espwaverider-esp32s3 --features device-benchmarks
Pop-Location
```

Flash and monitor the device benchmark image:

```powershell
& 'C:\Users\jd\.cargo\bin\espflash.exe' list-ports
& 'C:\Users\jd\.cargo\bin\espflash.exe' flash -p COM17 -c esp32s3 -M --non-interactive 'C:\git\LB-ESP32S3-MMWave-Testing\rust\target\xtensa-esp32s3-none-elf\release\espwaverider-esp32s3'
```

The Rust firmware now exposes a newline-delimited serial command surface on the same USB Serial/JTAG link used by `espflash`. After opening `COM17`, send one command per line and read one JSON snapshot response per line.

Currently implemented live commands:

- `status`
- `ha_status`
- `snapshot`
- `ha_room_config:room-id|role|pose_x|pose_y|heading|room_width|room_height`
- `tuning_config:max_range|min_gate_energy|sensitivity|presence_hold|min_active_gates|min_activity_score|led_enabled|led_brightness`
- `runtime_benchmark`
- `firmware_sync`

Example manual validation from PowerShell:

```powershell
$port = [System.IO.Ports.SerialPort]::new('COM17', 115200)
$port.NewLine = "`n"
$port.ReadTimeout = 1000
$port.Open()
try {
	$port.WriteLine('status')
	$port.ReadLine()
	$port.WriteLine('ha_room_config:room-lab|fixed|12|-34|90|700|500')
	$port.ReadLine()
	$port.WriteLine('runtime_benchmark')
	1..5 | ForEach-Object { $port.ReadLine() }
}
finally {
	$port.Close()
}
```

The benchmark firmware prints total elapsed microseconds for the full run and per-iteration nanoseconds for easier comparison against the host Criterion output, for example:

```text
device_bench iterations=1000
device_bench parse_generic_fixture total_us=304293 per_iter_ns=304293
device_bench derive_metrics_fixture total_us=1899 per_iter_ns=1899
device_bench detection_candidate_fixture total_us=690 per_iter_ns=690
```

Automated device-to-device A/B collection from the repository root:

```powershell
& '.\scripts\collect-ab-benchmarks.ps1'
```

That script uploads the C++ firmware, runs `runtime_benchmark` over HTTP, flashes the Rust benchmark image, captures the Rust serial benchmark output, writes `artifacts/ab-benchmark-latest.json`, and restores the C++ image afterward.

Workspace metadata validation:

```powershell
& 'C:\Users\jd\.cargo\bin\cargo.exe' metadata --manifest-path rust/Cargo.toml --no-deps
```

