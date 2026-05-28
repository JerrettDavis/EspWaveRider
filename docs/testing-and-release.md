# Testing And Releases

This page covers the validation ladder for the repository: fast hosted-safe checks, hardware-backed benchmarks, feature-status tracking, and release publication.

## Local tests

Run the fast hosted-safe suites from the repository root:

```powershell
npm run test:unit
npm run test:integration
npm run test:e2e:offline
```

Run the live hardware-backed suite locally when you have reachable devices on the LAN:

```powershell
npm run test:e2e:local
```

Recommended order:

1. `npm run test:unit`
2. `npm run test:integration`
3. `npm run test:e2e:offline`
4. target-specific firmware build
5. hardware-backed benchmark or live e2e when the change affects runtime behavior

## Firmware validation

PlatformIO builds are the primary firmware validation path:

```powershell
platformio run --environment esp32-s3-devkitm-1
```

Repeat with other environments when changing board-specific behavior.

Rust parity validation is tracked separately through host-side Rust tests, host benchmarks, and device A/B benchmarks. The current validated behavior status lives in [Parity Matrix](parity-matrix.md).

## Benchmark and comparison workflows

Generate the current C++ vs Rust device benchmark report:

```powershell
& '.\scripts\collect-ab-benchmarks.ps1'
```

Generate the current firmware size report:

```powershell
npm run report:firmware-sizes
```

Read the latest summary in [Benchmarks And Comparison](benchmarks-and-comparison.md).

For the release-operator view, see [Release Guide](release-guide.md).

## GitHub automation

The repository includes hosted automation for:

- Firmware builds across supported PlatformIO environments.
- DocFX documentation build validation on pushes and pull requests.
- Rendered docs preview artifacts for pull-request review.
- Python unit and integration tests for the embed pipeline.
- Offline Playwright dashboard integration coverage.
- Semantic-release dry-run validation in CI so release note generation is exercised before merge.
- Semantic-release driven versioning and GitHub release publication.

Hardware-backed live e2e remains separate because GitHub-hosted runners cannot reach local devices.

## Release behavior

Conventional commits drive semantic versioning:

- `feat:` creates a minor release.
- `fix:` and `perf:` create a patch release.
- `BREAKING CHANGE:` or `!` creates a major release.

Published release assets include one binary per supported board target and a matching `.sha256` checksum. Asset names are board-specific rather than raw PlatformIO environment IDs so users can distinguish the validated `lonely-esp32-s3-devkitm-1` release binary from other ESP32-S3 boards.

| PlatformIO environment | Published release target |
| --- | --- |
| `esp32-s3-devkitm-1` | `lonely-esp32-s3-devkitm-1` |
| `esp32dev-uart1` | `esp32-generic-uart1` |
| `heltec-wifi-lora-32-v3` | `heltec-wifi-lora-32-v3` |
| `heltec-wifi-lora-32-v4` | `heltec-wifi-lora-32-v4-compatible` |

Release checklist:

1. Hosted-safe tests are green.
2. Primary firmware target builds cleanly.
3. Docs build cleanly.
4. Release asset naming matches the intended board target.
5. If runtime behavior changed, parity matrix and benchmark docs are refreshed.

## Build this docs site locally

Install DocFX and build the site from the repository root:

```powershell
dotnet tool update --global docfx
docfx docs/docfx.json
```

The generated site is written to `docs/_site`.
