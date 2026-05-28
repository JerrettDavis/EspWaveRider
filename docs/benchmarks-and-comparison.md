# Benchmarks And Comparison

This page consolidates the current implementation status, the latest measured C++ vs Rust benchmark results, and the latest firmware size snapshot.

## At a glance

| Area | C++ firmware | Rust ESP32-S3 port | Current position |
| --- | --- | --- | --- |
| Shipping baseline | Yes | Not yet | C++ remains the production baseline |
| Snapshot and command contract | Full | Full | Parity validated |
| Hosted dashboard | Full | Full | Parity validated |
| MQTT over TCP | Full | Full | Parity validated |
| UDP discovery and room fusion | Full | Full | Parity validated |
| OTA apply | Full | Partial | Rust still stops before apply |
| BLE observation | Full | Partial | Rust scanner exists but is disabled for stability |

## Latest device benchmark summary

Source: `artifacts/ab-benchmark-latest.md`, generated `2026-05-24T05:57:00.8327205Z`.

| Slice | C++ per iter ns | Rust per iter ns | Winner | Delta |
| --- | ---: | ---: | --- | --- |
| `parse_command_fixture` | 17831 | 674 | Rust | Rust faster by 26.46x |
| `parse_room_config_fixture` | 122580 | 14296 | Rust | Rust faster by 8.57x |
| `parse_tuning_config_fixture` | 96911 | 14815 | Rust | Rust faster by 6.54x |
| `parse_generic_fixture` | 352801 | 63811 | Rust | Rust faster by 5.53x |
| `derive_metrics_fixture` | 2623 | 1903 | Rust | Rust faster by 1.38x |
| `detection_candidate_fixture` | 631 | 649 | C++ | C++ faster by 1.03x |

Interpretation:

- Rust won 5 of 6 measured slices in the latest device run.
- C++ still holds a slight lead in `detection_candidate_fixture`.
- The largest measured Rust gains are in command and config parsing.

## Latest size snapshot

Source: `artifacts/firmware-size-report.md`, generated `2026-05-28T04:15:48Z`.

| Artifact | Bytes | KB |
| --- | ---: | ---: |
| Rust ELF | 1089024 | 1063.5 |
| C++ BIN | 1392608 | 1359.97 |
| C++ ELF | 3182176 | 3107.59 |

ELF section summary:

| Artifact | text | data | bss | dec |
| --- | ---: | ---: | ---: | ---: |
| Rust ELF | 757425 | 12180 | 591720 | 1361325 |
| C++ ELF | 996382 | 412124 | 1028881 | 2437387 |

## Runtime and stability notes

- The decisive Rust stability improvement was switching the configured runtime from AP+STA coexistence to station-only.
- The current stable Rust runtime also uses `CpuClock::_80MHz` and `PowerSaveMode::Maximum`.
- SoftAP remains available for the unconfigured fallback path.
- BLE scanning remains the main live-runtime gap on the Rust board.

## How to reproduce

Collect the current device A/B benchmark report:

```powershell
& '.\scripts\collect-ab-benchmarks.ps1'
```

Generate the current firmware size report:

```powershell
npm run report:firmware-sizes
```

Run the Rust host benchmark directly:

```powershell
& 'C:\Users\jd\.cargo\bin\cargo.exe' bench --manifest-path rust/crates/espwaverider-core/Cargo.toml --bench parity_bench
```

## Read this with the parity matrix

This page summarizes performance and size. It is not the source of truth for feature completeness.

Use [Parity Matrix](parity-matrix.md) for behavior and validation status.