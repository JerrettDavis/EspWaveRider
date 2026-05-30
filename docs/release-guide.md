# Release Guide

This page describes what a good EspWaveRider release contains, how release assets are produced, and what should be true before a release is considered production-ready.

## Release goals

A release should give operators enough information to answer four questions quickly:

1. What changed?
2. Which board binary should I flash?
3. What was validated?
4. Are there any known limits or compatibility notes?

## What the pipeline produces

On every qualifying push to `main`, semantic-release:

- calculates the next semantic version from conventional commits
- updates `CHANGELOG.md`
- creates the GitHub release tag
- builds board-specific firmware binaries
- attaches `.bin` and `.sha256` assets to the GitHub release

Published board targets:

| PlatformIO environment | Published release target |
| --- | --- |
| `esp32-s3-devkitm-1` | `lonely-esp32-s3-devkitm-1` |
| `esp32dev-uart1` | `esp32-generic-uart1` |
| `heltec-wifi-lora-32-v3` | `heltec-wifi-lora-32-v3` |
| `heltec-wifi-lora-32-v4` | `heltec-wifi-lora-32-v4-compatible` |

## Release note shape

Generated release notes and changelog entries are grouped into product-readable sections:

- Features
- Fixes
- Performance
- Documentation
- Refactoring
- Tests
- Build System
- CI

That keeps the GitHub release page readable even when the underlying commits are fully automated.

## Release guardrails

The repository now validates two release-adjacent checks before merge:

- `docfx docs/docfx.json` runs in CI so documentation link and build regressions fail early.
- the CI workflow uploads the rendered `docs/_site` output as a review artifact so docs changes can be inspected before merge.
- `semantic-release --dry-run --no-ci` runs in CI so release-note generation and semantic version calculation are exercised before the release workflow runs on `main`.

Local equivalents:

```powershell
npm run docs:build
npm run release:preview
```

## Release readiness checklist

Minimum bar:

1. Hosted-safe test lanes are green.
2. The primary board firmware builds cleanly.
3. Board-specific asset names are correct.
4. Docs match the current feature surface and known limits.

Recommended bar when runtime behavior changed:

1. Refresh the parity matrix.
2. Refresh the benchmark and firmware-size reports.
3. Confirm operator docs still match the dashboard and snapshot surfaces.
4. Note any cross-board or C++ vs Rust compatibility caveats in commit messages so they flow into release notes.
5. If shipping Rust artifacts, state whether they were built with `https-ota`, `ble-scan`, both, or neither.

## Operator guidance for GitHub releases

- Match the binary suffix to the actual board target.
- Verify the corresponding `.sha256` file before distribution.
- Do not treat `lonely-esp32-s3-devkitm-1` as a generic ESP32-S3 image.
- Treat C++ as the full-feature production baseline today.
- Treat the Rust default build as the stable Wi-Fi/MQTT/UDP/hosted-UI baseline, not as a full-feature replacement for C++.
- Do not imply Rust OTA apply or live BLE observation parity unless the shipped Rust artifact was explicitly built and validated with `https-ota` and/or `ble-scan`.

## Related docs

- [Getting Started](getting-started.md)
- [Benchmarks And Comparison](benchmarks-and-comparison.md)
- [Parity Matrix](parity-matrix.md)
- [Testing And Releases](testing-and-release.md)