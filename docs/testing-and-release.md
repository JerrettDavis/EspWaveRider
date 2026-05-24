# Testing And Releases

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

## Firmware validation

PlatformIO builds are the primary firmware validation path:

```powershell
platformio run --environment esp32-s3-devkitm-1
```

Repeat with other environments when changing board-specific behavior.

## GitHub automation

The repository includes hosted automation for:

- Firmware builds across supported PlatformIO environments.
- Python unit and integration tests for the embed pipeline.
- Offline Playwright dashboard integration coverage.
- Semantic-release driven versioning and GitHub release publication.

Hardware-backed live e2e remains separate because GitHub-hosted runners cannot reach local devices.

## Release behavior

Conventional commits drive semantic versioning:

- `feat:` creates a minor release.
- `fix:` and `perf:` create a patch release.
- `BREAKING CHANGE:` or `!` creates a major release.

Published release assets include one binary per supported environment and a matching `.sha256` checksum.

## Build this docs site locally

Install DocFX and build the site from the repository root:

```powershell
dotnet tool update --global docfx
docfx docs/docfx.json
```

The generated site is written to `docs/_site`.
