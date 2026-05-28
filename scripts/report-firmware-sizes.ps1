param(
    [string]$WorkspaceRoot = 'C:\git\LB-ESP32S3-MMWave-Testing',
    [string]$XtensaSizePath = 'C:\Users\jd\.rustup\toolchains\esp\xtensa-esp-elf\bin\xtensa-esp32s3-elf-size.exe',
    [string]$JsonReportPath = 'C:\git\LB-ESP32S3-MMWave-Testing\artifacts\firmware-size-report.json',
    [string]$MarkdownReportPath = 'C:\git\LB-ESP32S3-MMWave-Testing\artifacts\firmware-size-report.md'
)

$ErrorActionPreference = 'Stop'

function Assert-PathExists {
    param(
        [string]$Path,
        [string]$Label
    )

    if (-not (Test-Path $Path)) {
        throw "$Label was not found at $Path"
    }
}

function Get-ArtifactInfo {
    param(
        [string]$Label,
        [string]$Path
    )

    Assert-PathExists -Path $Path -Label $Label
    $item = Get-Item $Path

    return [ordered]@{
        label = $Label
        path = $item.FullName
        size_bytes = [int64]$item.Length
        size_kb = [math]::Round($item.Length / 1KB, 2)
    }
}

function Get-ElfSections {
    param(
        [string]$Label,
        [string]$ElfPath,
        [string]$SizeToolPath
    )

    Assert-PathExists -Path $SizeToolPath -Label 'xtensa size tool'
    Assert-PathExists -Path $ElfPath -Label $Label

    $output = & $SizeToolPath $ElfPath
    if ($LASTEXITCODE -ne 0) {
        throw "xtensa size failed for $ElfPath"
    }

    $lines = @($output | Where-Object { $_.Trim().Length -gt 0 })
    if ($lines.Count -lt 2) {
        throw "Unexpected size output for $ElfPath"
    }

    $parts = ($lines[1] -split '\s+') | Where-Object { $_.Length -gt 0 }
    if ($parts.Count -lt 5) {
        throw "Unable to parse section totals for $ElfPath"
    }

    return [ordered]@{
        label = $Label
        text = [int64]$parts[0]
        data = [int64]$parts[1]
        bss = [int64]$parts[2]
        dec = [int64]$parts[3]
        hex = $parts[4]
    }
}

function New-MarkdownReport {
    param(
        [hashtable]$Report
    )

    $lines = New-Object System.Collections.Generic.List[string]
    $null = $lines.Add('# Firmware Size Report')
    $null = $lines.Add('')
    $null = $lines.Add("Generated: $($Report.generated_at_utc)")
    $null = $lines.Add('')
    $null = $lines.Add('## Artifact Sizes')
    $null = $lines.Add('')
    $null = $lines.Add('| Artifact | Path | Bytes | KB |')
    $null = $lines.Add('|---|---|---:|---:|')

    foreach ($artifact in @($Report.rust.elf, $Report.cpp.bin, $Report.cpp.elf, $Report.cpp.map)) {
        $null = $lines.Add("| $($artifact.label) | ``$($artifact.path)`` | $($artifact.size_bytes) | $($artifact.size_kb) |")
    }

    $null = $lines.Add('')
    $null = $lines.Add('## ELF Sections')
    $null = $lines.Add('')
    $null = $lines.Add('| Artifact | text | data | bss | dec | hex |')
    $null = $lines.Add('|---|---:|---:|---:|---:|---|')

    foreach ($sections in @($Report.rust.sections, $Report.cpp.sections)) {
        $null = $lines.Add("| $($sections.label) | $($sections.text) | $($sections.data) | $($sections.bss) | $($sections.dec) | $($sections.hex) |")
    }

    return ($lines -join [Environment]::NewLine) + [Environment]::NewLine
}

$rustElfPath = Join-Path $WorkspaceRoot 'rust\target\xtensa-esp32s3-none-elf\release\espwaverider-esp32s3'
$cppBinPath = Join-Path $WorkspaceRoot '.pio\build\esp32-s3-devkitm-1\firmware.bin'
$cppElfPath = Join-Path $WorkspaceRoot '.pio\build\esp32-s3-devkitm-1\firmware.elf'
$cppMapPath = Join-Path $WorkspaceRoot '.pio\build\esp32-s3-devkitm-1\firmware.map'

$report = [ordered]@{
    generated_at_utc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    rust = [ordered]@{
        elf = Get-ArtifactInfo -Label 'Rust ELF' -Path $rustElfPath
        sections = Get-ElfSections -Label 'Rust ELF' -ElfPath $rustElfPath -SizeToolPath $XtensaSizePath
    }
    cpp = [ordered]@{
        bin = Get-ArtifactInfo -Label 'C++ BIN' -Path $cppBinPath
        elf = Get-ArtifactInfo -Label 'C++ ELF' -Path $cppElfPath
        map = Get-ArtifactInfo -Label 'C++ MAP' -Path $cppMapPath
        sections = Get-ElfSections -Label 'C++ ELF' -ElfPath $cppElfPath -SizeToolPath $XtensaSizePath
    }
}

$jsonDirectory = Split-Path -Parent $JsonReportPath
$markdownDirectory = Split-Path -Parent $MarkdownReportPath
New-Item -ItemType Directory -Force -Path $jsonDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $markdownDirectory | Out-Null

$report | ConvertTo-Json -Depth 6 | Set-Content -Path $JsonReportPath
New-MarkdownReport -Report $report | Set-Content -Path $MarkdownReportPath

$report | ConvertTo-Json -Depth 6 -Compress | Write-Output