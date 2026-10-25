# Measures KeyShorts memory use. Starts a hidden instance if none is running,
# then stops it again so the script leaves the system as it found it.

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build\Release\KeyShorts.exe'
$processName = 'KeyShorts'

$started = $null
$process = Get-Process -Name $processName -ErrorAction SilentlyContinue |
    Sort-Object StartTime | Select-Object -First 1

if (-not $process) {
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "KeyShorts.exe not found at $exe. Run .\build.ps1 first."
    }
    $started = Start-Process -FilePath $exe -ArgumentList '--tray' -PassThru
    Start-Sleep -Milliseconds 900
    $process = $started
}

$process.Refresh()

$privateBytes = $process.PrivateMemorySize64
$workingSet = $process.WorkingSet64

$privateWorkingSet = $null
try {
    $perf = Get-CimInstance Win32_PerfFormattedData_PerfProc_Process `
        -Filter "Name='$processName'" -ErrorAction Stop |
        Select-Object -First 1
    if ($perf) { $privateWorkingSet = [int64]$perf.WorkingSetPrivate }
} catch {
}

function Format-MB([Nullable[Int64]]$bytes) {
    if ($null -eq $bytes) { return 'n/a' }
    return '{0:N2} MB' -f ($bytes / 1MB)
}

"{0} (PID {1})" -f $process.ProcessName, $process.Id
"  Private bytes        {0}" -f (Format-MB $privateBytes)
"  Private working set  {0}" -f (Format-MB $privateWorkingSet)
"  Total working set    {0}" -f (Format-MB $workingSet)

if ($started) {
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    'Stopped the test instance.'
}
