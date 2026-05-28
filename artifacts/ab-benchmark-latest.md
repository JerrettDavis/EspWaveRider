# C++ vs Rust Device Benchmark Report

Generated at: 2026-05-24T05:57:00.8327205Z

Device URL: http://10.0.107.148
Upload Port: COM17

## Summary

- parse_command_fixture: winner=Rust, speedup=26.46x, cpp=17831 ns, rust=674 ns, delta_ns=-17157
- parse_room_config_fixture: winner=Rust, speedup=8.57x, cpp=122580 ns, rust=14296 ns, delta_ns=-108284
- parse_tuning_config_fixture: winner=Rust, speedup=6.54x, cpp=96911 ns, rust=14815 ns, delta_ns=-82096
- parse_generic_fixture: winner=Rust, speedup=5.53x, cpp=352801 ns, rust=63811 ns, delta_ns=-288990
- derive_metrics_fixture: winner=Rust, speedup=1.38x, cpp=2623 ns, rust=1903 ns, delta_ns=-720
- detection_candidate_fixture: winner=C++, speedup=1.03x, cpp=631 ns, rust=649 ns, delta_ns=18

## Raw Measurements

| Slice | C++ total us | C++ per iter ns | Rust total us | Rust per iter ns | Rust/C++ ratio | Winner |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| parse_command_fixture | 17831 | 17831 | 674 | 674 | 0.038 | Rust |
| parse_room_config_fixture | 122580 | 122580 | 14296 | 14296 | 0.117 | Rust |
| parse_tuning_config_fixture | 96911 | 96911 | 14815 | 14815 | 0.153 | Rust |
| parse_generic_fixture | 352801 | 352801 | 63811 | 63811 | 0.181 | Rust |
| derive_metrics_fixture | 2623 | 2623 | 1903 | 1903 | 0.726 | Rust |
| detection_candidate_fixture | 631 | 631 | 649 | 649 | 1.029 | C++ |

## Detailed Outputs

### C++ benchmark payload

```json
{
  "measured_at_ms": 18620,
  "iterations": 1000,
  "parse_command_fixture": {
    "total_us": 17831,
    "per_iter_ns": 17831
  },
  "parse_room_config_fixture": {
    "total_us": 122580,
    "per_iter_ns": 122580
  },
  "parse_tuning_config_fixture": {
    "total_us": 96911,
    "per_iter_ns": 96911
  },
  "parse_generic_fixture": {
    "total_us": 352801,
    "per_iter_ns": 352801
  },
  "derive_metrics_fixture": {
    "total_us": 2623,
    "per_iter_ns": 2623
  },
  "detection_candidate_fixture": {
    "total_us": 631,
    "per_iter_ns": 631
  },
  "detection_candidate": true,
  "people_estimate": 0,
  "active_gate_count": 2,
  "activity_score": 100,
  "dominant_gate_distance_cm": 35
}
```

### Rust serial benchmark output

```text
Chip type:         esp32s3 (revision v0.2)
Crystal frequency: 40 MHz
Flash size:        16MB
Features:          WiFi, BLE, Embedded Flash
MAC address:       3c:dc:75:71:53:dc
App/part. size:    135,776/16,384,000 bytes, 0.83%
ESP-ROM:esp32s3-20210327
Build:Mar 27 2021
rst:0x15 (USB_UART_CHIP_RESET),boot:0xa (SPI_FAST_FLASH_BOOT)
Saved PC:0x40378eb1
SPIWP:0xee
mode:DIO, clock div:2
load:0x3fce2820,len:0x158c
load:0x403c8700,len:0xd24
load:0x403cb700,len:0x2f34
entry 0x403c8924
I (27) boot: ESP-IDF v5.5.1-838-gd66ebb86d2e 2nd stage bootloader
I (27) boot: compile time Nov 26 2025 12:27:56
I (27) boot: Multicore bootloader
I (29) boot: chip revision: v0.2
I (31) boot: efuse block revision: v1.3
I (35) boot.esp32s3: Boot SPI Speed : 40MHz
I (39) boot.esp32s3: SPI Mode       : DIO
I (43) boot.esp32s3: SPI Flash Size : 16MB
I (47) boot: Enabling RNG early entropy source...
I (51) boot: Partition Table:
I (54) boot: ## Label            Usage          Type ST Offset   Length
I (60) boot:  0 nvs              WiFi data        01 02 00009000 00006000
I (66) boot:  1 phy_init         RF data          01 01 0000f000 00001000
I (73) boot:  2 factory          factory app      00 00 00010000 00fa0000
I (80) boot: End of partition table
I (83) esp_image: segment 0: paddr=00010020 vaddr=3c000020 size=056ach ( 22188) map
I (96) esp_image: segment 1: paddr=000156d4 vaddr=3fc89774 size=00a34h (  2612) load
I (98) esp_image: segment 2: paddr=00016110 vaddr=40378000 size=01774h (  6004) load
I (107) esp_image: segment 3: paddr=0001788c vaddr=00000000 size=0878ch ( 34700) 
I (121) esp_image: segment 4: paddr=00020020 vaddr=42010020 size=11218h ( 70168) map
I (139) boot: Loaded app from partition at offset 0x10000
I (139) boot: Disabling RNG early entropy source...
device_bench iterations=1000
device_bench parse_command_fixture total_us=675 per_iter_ns=675
device_bench parse_room_config_fixture total_us=14295 per_iter_ns=14295
device_bench parse_tuning_config_fixture total_us=14814 per_iter_ns=14814
device_bench parse_generic_fixture total_us=63811 per_iter_ns=63811
device_bench derive_metrics_fixture total_us=1904 per_iter_ns=1904
device_bench detection_candidate_fixture total_us=649 per_iter_ns=649
EspWaveRider Rust bootstrap on ESP32-S3 DevKitM-1
Commands: status, ha_status, ha_room_config:..., tuning_config:..., runtime_benchmark, firmware_sync
ESP-ROM:esp32s3-20210327
Build:Mar 27 2021
rst:0x15 (USB_UART_CHIP_RESET),boot:0xa (SPI_FAST_FLASH_BOOT)
Saved PC:0x4201266a
SPIWP:0xee
mode:DIO, clock div:2
load:0x3fce2820,len:0x158c
load:0x403c8700,len:0xd24
load:0x403cb700,len:0x2f34
entry 0x403c8924
I (27) boot: ESP-IDF v5.5.1-838-gd66ebb86d2e 2nd stage bootloader
I (27) boot: compile time Nov 26 2025 12:27:56
I (27) boot: Multicore bootloader
I (29) boot: chip revision: v0.2
I (31) boot: efuse block revision: v1.3
I (35) boot.esp32s3: Boot SPI Speed : 40MHz
I (39) boot.esp32s3: SPI Mode       : DIO
I (43) boot.esp32s3: SPI Flash Size : 16MB
I (47) boot: Enabling RNG early entropy source...
I (51) boot: Partition Table:
I (54) boot: ## Label            Usage          Type ST Offset   Length
I (60) boot:  0 nvs              WiFi data        01 02 00009000 00006000
I (66) boot:  1 phy_init         RF data          01 01 0000f000 00001000
I (73) boot:  2 factory          factory app      00 00 00010000 00fa0000
I (80) boot: End of partition table
I (83) esp_image: segment 0: paddr=00010020 vaddr=3c000020 size=056ach ( 22188) map
I (96) esp_image: segment 1: paddr=000156d4 vaddr=3fc89774 size=00a34h (  2612) load
I (98) esp_image: segment 2: paddr=00016110 vaddr=40378000 size=01774h (  6004) load
I (107) esp_image: segment 3: paddr=0001788c vaddr=00000000 size=0878ch ( 34700) 
I (121) esp_image: segment 4: paddr=00020020 vaddr=42010020 size=11218h ( 70168) map
I (139) boot: Loaded app from partition at offset 0x10000
I (139) boot: Disabling RNG early entropy source...
device_bench iterations=1000
device_bench parse_command_fixture total_us=674 per_iter_ns=674
device_bench parse_room_config_fixture total_us=14296 per_iter_ns=14296
device_bench parse_tuning_config_fixture total_us=14815 per_iter_ns=14815
device_bench parse_generic_fixture total_us=63811 per_iter_ns=63811
device_bench derive_metrics_fixture total_us=1903 per_iter_ns=1903
device_bench detection_candidate_fixture total_us=649 per_iter_ns=649
EspWaveRider Rust bootstrap on ESP32-S3 DevKitM-1
Commands: status, ha_status, ha_room_config:..., tuning_config:..., runtime_benchmark, firmware_sync

[2026-05-24T05:56:58Z INFO ] Serial port: 'COM17'
[2026-05-24T05:56:58Z INFO ] Connecting...
[2026-05-24T05:56:58Z INFO ] Using flash stub
[2026-05-24T05:57:00Z INFO ] Flashing has completed!
```

## Interpretation

- parse_command_fixture: Rust is faster by 26.46x in this run, so C++ is the current optimization target for this slice.
- parse_room_config_fixture: Rust is faster by 8.57x in this run, so C++ is the current optimization target for this slice.
- parse_tuning_config_fixture: Rust is faster by 6.54x in this run, so C++ is the current optimization target for this slice.
- parse_generic_fixture: Rust is faster by 5.53x in this run, so C++ is the current optimization target for this slice.
- derive_metrics_fixture: Rust is faster by 1.38x in this run, so C++ is the current optimization target for this slice.
- detection_candidate_fixture: C++ is faster by 1.03x in this run, so Rust is the current optimization target for this slice.
