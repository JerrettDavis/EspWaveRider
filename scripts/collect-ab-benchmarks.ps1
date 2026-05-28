param(
    [string]$CppDeviceUrl = 'http://10.0.107.148',
    [string]$UploadPort = 'COM17',
    [string]$PlatformIoPath = 'C:\Users\jd\.platformio\penv\Scripts\platformio.exe',
    [string]$CargoPath = 'C:\Users\jd\.cargo\bin\cargo.exe',
    [string]$EspflashPath = 'C:\Users\jd\.cargo\bin\espflash.exe',
    [string]$PlatformIoEnvironment = 'esp32-s3-devkitm-1',
    [string]$RustWorkspaceRoot = 'C:\git\LB-ESP32S3-MMWave-Testing\rust',
    [string]$RustElfPath = 'C:\git\LB-ESP32S3-MMWave-Testing\rust\target\xtensa-esp32s3-none-elf\release\espwaverider-esp32s3',
    [string]$RustExportScriptPath = 'C:\git\LB-ESP32S3-MMWave-Testing\rust\export-esp.ps1',
    [string]$ReportPath = 'C:\git\LB-ESP32S3-MMWave-Testing\artifacts\ab-benchmark-latest.json',
    [string]$ReportMarkdownPath = 'C:\git\LB-ESP32S3-MMWave-Testing\artifacts\ab-benchmark-latest.md',
    [int]$CppReadyTimeoutSeconds = 90,
    [int]$RustMonitorTimeoutSeconds = 60,
    [switch]$SkipCppUpload,
    [switch]$SkipRustBuild,
    [switch]$SkipRestoreCppFirmware
)

$ErrorActionPreference = 'Stop'

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Action
    )

    Write-Host "==> $Name"
    & $Action
}

function Assert-PathExists {
    param(
        [string]$Path,
        [string]$Label
    )

    if (-not (Test-Path $Path)) {
        throw "$Label was not found at $Path"
    }
}

function Invoke-ExternalCommand {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory
    )

    $quotedArguments = $Arguments | ForEach-Object {
        if ($_ -match '\s') {
            '"' + $_ + '"'
        }
        else {
            $_
        }
    }

    $commandText = @($FilePath) + $quotedArguments
    Write-Host ($commandText -join ' ')

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $FilePath
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    foreach ($argument in $Arguments) {
        [void]$psi.ArgumentList.Add($argument)
    }

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $psi
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()

    if ($stdout) {
        Write-Host $stdout.TrimEnd()
    }
    if ($stderr) {
        Write-Host $stderr.TrimEnd()
    }

    if ($process.ExitCode -ne 0) {
        throw "Command failed with exit code $($process.ExitCode): $FilePath"
    }

    return [pscustomobject]@{
        StdOut = $stdout
        StdErr = $stderr
    }
}

function Invoke-PowerShellCommand {
    param(
        [string]$Command,
        [string]$WorkingDirectory
    )

    return Invoke-ExternalCommand -FilePath 'powershell.exe' -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', $Command) -WorkingDirectory $WorkingDirectory
}

function Stop-LingeringEspflashProcesses {
    param(
        [string]$EspflashExe
    )

    $staleProcesses = Get-Process | Where-Object { $_.Path -eq $EspflashExe }
    foreach ($process in $staleProcesses) {
        Write-Host "Stopping stale espflash process $($process.Id)"
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
}

function Wait-ForCppDevice {
    param(
        [string]$BaseUrl,
        [int]$TimeoutSeconds
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        try {
            $snapshot = Invoke-RestMethod "$BaseUrl/api/snapshot"
            return $snapshot
        }
        catch {
            Start-Sleep -Seconds 1
        }
    } while ((Get-Date) -lt $deadline)

    throw "Timed out waiting for C++ device at $BaseUrl"
}

function Invoke-CppBenchmark {
    param(
        [string]$BaseUrl
    )

    $response = Invoke-RestMethod "$BaseUrl/api/command" -Method Post -ContentType 'text/plain' -Body 'runtime_benchmark'
    if (-not $response.runtime_benchmark) {
        throw 'C++ benchmark response did not include runtime_benchmark'
    }

    return $response.runtime_benchmark
}

function Invoke-RustBenchmarkBuild {
    param(
        [string]$WorkspaceRoot,
        [string]$ExportScriptPath,
        [string]$CargoExe
    )

    $command = "Push-Location '$WorkspaceRoot'; & '$ExportScriptPath'; & '$CargoExe' +esp build --release -p espwaverider-esp32s3 --features device-benchmarks; Pop-Location"
    [void](Invoke-PowerShellCommand -Command $command -WorkingDirectory $WorkspaceRoot)
}

function Get-SharedFileText {
    param(
        [string]$Path
    )

    if (-not (Test-Path $Path)) {
        return ''
    }

    $stream = [System.IO.File]::Open($Path,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::ReadWrite)

    try {
        $reader = [System.IO.StreamReader]::new($stream)
        try {
            return $reader.ReadToEnd()
        }
        finally {
            $reader.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Invoke-RustBenchmarkCapture {
    param(
        [string]$EspflashExe,
        [string]$Port,
        [string]$ElfPath,
        [int]$TimeoutSeconds,
        [string]$WorkingDirectory
    )

    $stdoutPath = [System.IO.Path]::GetTempFileName()
    $stderrPath = [System.IO.Path]::GetTempFileName()

    try {
        $startProcessArgs = @{
            FilePath = $EspflashExe
            ArgumentList = @('flash', '-p', $Port, '-c', 'esp32s3', '-M', '--non-interactive', $ElfPath)
            WorkingDirectory = $WorkingDirectory
            RedirectStandardOutput = $stdoutPath
            RedirectStandardError = $stderrPath
            NoNewWindow = $true
            PassThru = $true
        }
        $process = Start-Process @startProcessArgs

        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        $measurementLines = [ordered]@{}
        $iterations = $null

        do {
            $combined = ''
            $combined += Get-SharedFileText -Path $stdoutPath
            $stderrText = Get-SharedFileText -Path $stderrPath
            if ($stderrText) {
                $combined += [Environment]::NewLine + $stderrText
            }

            if ($combined -match 'device_bench iterations=(\d+)') {
                $iterations = [int]$Matches[1]
            }

            $lineMatches = [regex]::Matches($combined, 'device_bench\s+([a-z0-9_]+)\s+total_us=(\d+)\s+per_iter_ns=(\d+)')
            foreach ($lineMatch in $lineMatches) {
                $name = $lineMatch.Groups[1].Value
                $measurementLines[$name] = [pscustomobject]@{
                    total_us = [int64]$lineMatch.Groups[2].Value
                    per_iter_ns = [int64]$lineMatch.Groups[3].Value
                }
            }

            if ($iterations -and $measurementLines.Count -gt 0) {
                if (-not $process.HasExited) {
                    Stop-Process -Id $process.Id -Force
                }

                return [pscustomobject]@{
                    iterations = $iterations
                    measurements = [pscustomobject]$measurementLines
                    raw_output = $combined
                }
            }

            if ($process.HasExited) {
                break
            }

            Start-Sleep -Milliseconds 250
        } while ((Get-Date) -lt $deadline)

        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
        }

        $combined = Get-SharedFileText -Path $stdoutPath
        $stderrText = Get-SharedFileText -Path $stderrPath
        if ($stderrText) {
            $combined += [Environment]::NewLine + $stderrText
        }

        throw "Timed out capturing Rust benchmark output. Collected output:`n$combined"
    }
    finally {
        Remove-Item $stdoutPath, $stderrPath -ErrorAction SilentlyContinue
    }
}

function New-ComparisonRow {
    param(
        [string]$Name,
        [pscustomobject]$Cpp,
        [pscustomobject]$Rust
    )

    $ratio = if ($Cpp.per_iter_ns -gt 0) {
        [math]::Round($Rust.per_iter_ns / $Cpp.per_iter_ns, 3)
    }
    else {
        $null
    }

    [pscustomobject]@{
        slice = $Name
        cpp_total_us = [int64]$Cpp.total_us
        cpp_per_iter_ns = [int64]$Cpp.per_iter_ns
        rust_total_us = [int64]$Rust.total_us
        rust_per_iter_ns = [int64]$Rust.per_iter_ns
        rust_vs_cpp_ratio = $ratio
    }
}

function Get-BenchmarkMeasurements {
    param(
        [pscustomobject]$Benchmark
    )

    $measurements = [ordered]@{}

    foreach ($property in $Benchmark.PSObject.Properties) {
        if ($property.Name -in @('measured_at_ms', 'iterations', 'detection_candidate', 'people_estimate', 'active_gate_count', 'activity_score', 'dominant_gate_distance_cm', 'raw_output', 'measurements')) {
            continue
        }

        $value = $property.Value
        if ($null -ne $value -and $value.PSObject.Properties.Name -contains 'total_us' -and $value.PSObject.Properties.Name -contains 'per_iter_ns') {
            $measurements[$property.Name] = [pscustomobject]@{
                total_us = [int64]$value.total_us
                per_iter_ns = [int64]$value.per_iter_ns
            }
        }
    }

    if ($Benchmark.PSObject.Properties.Name -contains 'measurements' -and $Benchmark.measurements) {
        foreach ($property in $Benchmark.measurements.PSObject.Properties) {
            $measurements[$property.Name] = $property.Value
        }
    }

    return [pscustomobject]$measurements
}

function Get-SliceWinner {
    param(
        [pscustomobject]$Comparison
    )

    if ($Comparison.rust_per_iter_ns -lt $Comparison.cpp_per_iter_ns) {
        return 'Rust'
    }

    if ($Comparison.rust_per_iter_ns -gt $Comparison.cpp_per_iter_ns) {
        return 'C++'
    }

    return 'Tie'
}

function Get-SpeedupText {
    param(
        [pscustomobject]$Comparison
    )

    if ($Comparison.rust_per_iter_ns -eq $Comparison.cpp_per_iter_ns) {
        return '1.00x'
    }

    if ($Comparison.rust_per_iter_ns -lt $Comparison.cpp_per_iter_ns) {
        return ('{0:N2}x' -f ($Comparison.cpp_per_iter_ns / $Comparison.rust_per_iter_ns))
    }

    return ('{0:N2}x' -f ($Comparison.rust_per_iter_ns / $Comparison.cpp_per_iter_ns))
}

function New-MarkdownBenchmarkReport {
    param(
        [pscustomobject]$Report,
        [pscustomobject[]]$Comparisons
    )

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('# C++ vs Rust Device Benchmark Report')
    $lines.Add('')
    $lines.Add("Generated at: $($Report.generated_at_utc)")
    $lines.Add("")
    $lines.Add("Device URL: $($Report.cpp_device_url)")
    $lines.Add("Upload Port: $($Report.upload_port)")
    $lines.Add("")
    $lines.Add('## Summary')
    $lines.Add('')

    foreach ($comparison in $Comparisons) {
        $winner = Get-SliceWinner -Comparison $comparison
        $speedup = Get-SpeedupText -Comparison $comparison
        $deltaNs = [int64]($comparison.rust_per_iter_ns - $comparison.cpp_per_iter_ns)
        $lines.Add("- $($comparison.slice): winner=$winner, speedup=$speedup, cpp=$($comparison.cpp_per_iter_ns) ns, rust=$($comparison.rust_per_iter_ns) ns, delta_ns=$deltaNs")
    }

    $lines.Add('')
    $lines.Add('## Raw Measurements')
    $lines.Add('')
    $lines.Add('| Slice | C++ total us | C++ per iter ns | Rust total us | Rust per iter ns | Rust/C++ ratio | Winner |')
    $lines.Add('| --- | ---: | ---: | ---: | ---: | ---: | --- |')

    foreach ($comparison in $Comparisons) {
        $winner = Get-SliceWinner -Comparison $comparison
        $ratio = if ($null -eq $comparison.rust_vs_cpp_ratio) { 'n/a' } else { '{0:N3}' -f $comparison.rust_vs_cpp_ratio }
        $lines.Add("| $($comparison.slice) | $($comparison.cpp_total_us) | $($comparison.cpp_per_iter_ns) | $($comparison.rust_total_us) | $($comparison.rust_per_iter_ns) | $ratio | $winner |")
    }

    $lines.Add('')
    $lines.Add('## Detailed Outputs')
    $lines.Add('')
    $lines.Add('### C++ benchmark payload')
    $lines.Add('')
    $lines.Add('```json')
    $lines.Add(($Report.cpp | ConvertTo-Json -Depth 8))
    $lines.Add('```')
    $lines.Add('')
    $lines.Add('### Rust serial benchmark output')
    $lines.Add('')
    $lines.Add('```text')
    $lines.Add($Report.rust.raw_output.TrimEnd())
    $lines.Add('```')
    $lines.Add('')
    $lines.Add('## Interpretation')
    $lines.Add('')

    foreach ($comparison in $Comparisons) {
        $winner = Get-SliceWinner -Comparison $comparison
        $speedup = Get-SpeedupText -Comparison $comparison
        if ($winner -eq 'Tie') {
            $lines.Add("- $($comparison.slice): both implementations are effectively tied in this run.")
        }
        else {
            $loser = if ($winner -eq 'Rust') { 'C++' } else { 'Rust' }
            $lines.Add("- $($comparison.slice): $winner is faster by $speedup in this run, so $loser is the current optimization target for this slice.")
        }
    }

    return ($lines -join [Environment]::NewLine)
}

Assert-PathExists -Path $PlatformIoPath -Label 'PlatformIO executable'
Assert-PathExists -Path $CargoPath -Label 'Cargo executable'
Assert-PathExists -Path $EspflashPath -Label 'espflash executable'
Assert-PathExists -Path $RustWorkspaceRoot -Label 'Rust workspace root'
Assert-PathExists -Path $RustExportScriptPath -Label 'Rust export script'

$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $SkipCppUpload) {
    Invoke-Step -Name 'Upload C++ benchmark firmware' -Action {
        Stop-LingeringEspflashProcesses -EspflashExe $EspflashPath
        $uploadArgs = @{
            FilePath = $PlatformIoPath
            Arguments = @('run', '--environment', $PlatformIoEnvironment, '--target', 'upload', '--upload-port', $UploadPort)
            WorkingDirectory = $repoRoot
        }
        [void](Invoke-ExternalCommand @uploadArgs)
    }
}

Invoke-Step -Name 'Wait for C++ device API' -Action {
    [void](Wait-ForCppDevice -BaseUrl $CppDeviceUrl -TimeoutSeconds $CppReadyTimeoutSeconds)
}

$cppBenchmark = $null
Invoke-Step -Name 'Run C++ runtime benchmark' -Action {
    $script:cppBenchmark = Invoke-CppBenchmark -BaseUrl $CppDeviceUrl
    $cppBenchmark | ConvertTo-Json -Depth 8 | Write-Host
}

if (-not $SkipRustBuild) {
    Invoke-Step -Name 'Build Rust benchmark firmware' -Action {
        Invoke-RustBenchmarkBuild -WorkspaceRoot $RustWorkspaceRoot -ExportScriptPath $RustExportScriptPath -CargoExe $CargoPath
    }
}

Assert-PathExists -Path $RustElfPath -Label 'Rust benchmark ELF'

$rustBenchmark = $null
Invoke-Step -Name 'Flash and capture Rust benchmark firmware' -Action {
    Stop-LingeringEspflashProcesses -EspflashExe $EspflashPath
    $script:rustBenchmark = Invoke-RustBenchmarkCapture -EspflashExe $EspflashPath -Port $UploadPort -ElfPath $RustElfPath -TimeoutSeconds $RustMonitorTimeoutSeconds -WorkingDirectory $repoRoot
    $rustBenchmark | ConvertTo-Json -Depth 8 | Write-Host
}

$cppMeasurements = Get-BenchmarkMeasurements -Benchmark $cppBenchmark
$rustMeasurements = Get-BenchmarkMeasurements -Benchmark $rustBenchmark
$sliceNames = @($cppMeasurements.PSObject.Properties.Name | Where-Object { $rustMeasurements.PSObject.Properties.Name -contains $_ })

$comparisons = foreach ($sliceName in $sliceNames) {
    New-ComparisonRow -Name $sliceName -Cpp $cppMeasurements.$sliceName -Rust $rustMeasurements.$sliceName
}

$report = [pscustomobject]@{
    generated_at_utc = [DateTime]::UtcNow.ToString('o')
    cpp_device_url = $CppDeviceUrl
    upload_port = $UploadPort
    cpp = $cppBenchmark
    rust = $rustBenchmark
    comparisons = $comparisons
}

$reportDirectory = Split-Path -Parent $ReportPath
if (-not (Test-Path $reportDirectory)) {
    [void](New-Item -ItemType Directory -Path $reportDirectory -Force)
}

$report | ConvertTo-Json -Depth 8 | Set-Content -Path $ReportPath
$markdownReport = New-MarkdownBenchmarkReport -Report $report -Comparisons $comparisons
$markdownReport | Set-Content -Path $ReportMarkdownPath

Write-Host '==> A/B comparison summary'
$comparisons | Format-Table -AutoSize | Out-String | Write-Host
Write-Host "Saved JSON report to $ReportPath"
Write-Host "Saved Markdown report to $ReportMarkdownPath"

if (-not $SkipRestoreCppFirmware) {
    Invoke-Step -Name 'Restore C++ firmware on the test board' -Action {
        Stop-LingeringEspflashProcesses -EspflashExe $EspflashPath
        $restoreArgs = @{
            FilePath = $PlatformIoPath
            Arguments = @('run', '--environment', $PlatformIoEnvironment, '--target', 'upload', '--upload-port', $UploadPort)
            WorkingDirectory = $repoRoot
        }
        [void](Invoke-ExternalCommand @restoreArgs)
    }
}