param(
    [string]$Sdcc = "sdcc",
    [string]$Packihx = "packihx",
    [switch]$Clean,
    [switch]$Tests,
    [string]$BuildId = "",
    [switch]$SafeBringup
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if ($Tests -and $SafeBringup) { throw "Tests and SafeBringup are separate builds." }
if ($Tests) {
    $BuildDir = Join-Path $Root "build_test"
} elseif ($SafeBringup) {
    $BuildDir = Join-Path $Root "build_safe"
} else {
    $BuildDir = Join-Path $Root "build"
}
$IncludeDir = Join-Path $Root "include"
$SrcDir = Join-Path $Root "src"

if ($Clean -and (Test-Path $BuildDir)) {
    $resolvedRoot = (Resolve-Path $Root).Path
    $resolvedBuild = (Resolve-Path $BuildDir).Path
    $rootPrefix = $resolvedRoot.TrimEnd([char[]]"\/") + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedBuild.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean outside the firmware directory."
    }
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$protocolPath = Join-Path (Split-Path -Parent $Root) "protocol/protocol.json"
$protocol = Get-Content -LiteralPath $protocolPath -Raw | ConvertFrom-Json
if ($protocol.format -ne 'buddy.da64-report-protocol' -or $protocol.schema -ne 1 -or
    $protocol.cc2_profile.id -ne 'CC2_LITHIUM' -or $protocol.cc2_profile.chemistry -ne 'LITHIUM' -or
    $protocol.cc2_profile.service_baud -ne 19200) { throw 'Invalid lithium report profile.' }
$protocolHash = (Get-FileHash -LiteralPath $protocolPath -Algorithm SHA256).Hash.ToLowerInvariant()
if (-not $BuildId) {
    try { $zone = [TimeZoneInfo]::FindSystemTimeZoneById('Europe/Oslo') }
    catch { $zone = [TimeZoneInfo]::FindSystemTimeZoneById('W. Europe Standard Time') }
    $stamp = [TimeZoneInfo]::ConvertTimeFromUtc([DateTime]::UtcNow, $zone).ToString('yyyyMMdd-HHmmss')
    $run = if ($env:GITHUB_RUN_NUMBER) { $env:GITHUB_RUN_NUMBER } else { '0' }
    $BuildId = "$stamp.$run"
}
if ($BuildId -notmatch '^\d{8}-\d{6}\.\d+$') { throw 'Invalid release build ID.' }
$aggregate = @($protocol.cc2_profile.aggregate_reports)
@(
    '#ifndef CC2_BUILD_H'
    '#define CC2_BUILD_H'
    ('#define CC2_BUILD_ID "{0}"' -f $BuildId)
    ('#define CC2_PROTOCOL_SHA256 "{0}"' -f $protocolHash)
    ('#define CC2_AGGREGATE_COUNT {0}u' -f $aggregate.Count)
    ('static const uint8_t __code cc2_aggregate_reports[] = {{ {0} }};' -f ($aggregate -join ', '))
    ('static const uint8_t __code cc2_report_versions[] = {{ {0} }};' -f ((0..15 | ForEach-Object {
        $value = $protocol.cc2_profile.supported_reports.PSObject.Properties["$_"]
        if ($value) { [int]$value.Value } else { 0 }
    }) -join ', '))
    '#endif'
) | Set-Content -LiteralPath (Join-Path $BuildDir 'cc2_build.h') -Encoding ascii

$sdccCommand = Get-Command $Sdcc -ErrorAction SilentlyContinue
$packihxCommand = Get-Command $Packihx -ErrorAction SilentlyContinue
if (-not $sdccCommand) {
    throw "sdcc was not found. Install SDCC or pass -Sdcc C:\path\to\sdcc.exe"
}
if (-not $packihxCommand) {
    throw "packihx was not found. Install SDCC or pass -Packihx C:\path\to\packihx.exe"
}

$sdccDir = Split-Path -Parent $sdccCommand.Source
$packihxDir = Split-Path -Parent $packihxCommand.Source
$shimDir = Join-Path $BuildDir "tool_shims"
New-Item -ItemType Directory -Force -Path $shimDir | Out-Null

# Some Windows SDCC archives ship the compiler helper as `cc1` without the
# extension that CreateProcess expects. Keep the workaround local to build/.
$cc1NoExtension = Join-Path $sdccDir "cc1"
$cc1Executable = Join-Path $sdccDir "cc1.exe"
if ((Test-Path $cc1NoExtension) -and -not (Test-Path $cc1Executable)) {
    Copy-Item -LiteralPath $cc1NoExtension -Destination (Join-Path $shimDir "cc1.exe") -Force
}
$env:Path = ((@($shimDir, $sdccDir, $packihxDir) + @($env:Path)) -join [IO.Path]::PathSeparator)

$assembler = Join-Path $sdccDir "sdas8051.exe"
if (-not (Test-Path $assembler)) {
    $assemblerCommand = Get-Command "sdas8051" -ErrorAction SilentlyContinue
    if (-not $assemblerCommand) {
        throw "sdas8051 was not found beside sdcc or on PATH."
    }
    $assembler = $assemblerCommand.Source
}

$commonFlags = @(
    "-mmcs51",
    "--model-large",
    "--stack-auto",
    "--std-sdcc99",
    "--opt-code-size",
    "-I$BuildDir",
    "-I$IncludeDir"
)
if ($SafeBringup) {
    $commonFlags += "-DCC2_SAFE_BRINGUP=1"
}

$relFiles = @()
$sources = @(
    "hardware.c",
    "uart.c",
    "timebase.c",
    "speed.c",
    "i2c_bus.c",
    "display.c",
    "display_eeprom.c",
    "vehicle_eeprom.c",
    "drive.c",
    "lithium.c",
    "protocol.c",
    "settings.c",
    "legacy.c",
    "safety.c",
    "controller.c",
    "reports.c",
    "commands.c",
    "main.c"
)
if ($Tests) { $sources = @("lithium.c", "protocol.c", "settings.c", "legacy.c", "../tests/test_core.c") }
foreach ($source in $sources) {
    $sourcePath = Join-Path $SrcDir $source
    if ($Tests -and $source.StartsWith("../tests")) {
        $sourcePath = Join-Path (Split-Path -Parent $Root) "tests/test_core.c"
    }
    $relPath = Join-Path $BuildDir ([IO.Path]::ChangeExtension((Split-Path -Leaf $source), ".rel"))
    & $sdccCommand.Source @commonFlags -c $sourcePath -o $relPath
    if ($LASTEXITCODE -ne 0) {
        throw "SDCC compile failed for $source"
    }
    $relFiles += $relPath
}

if (-not $Tests) {
$clockRel = Join-Path $BuildDir "startup_clock.rel"
& $assembler -los $clockRel (Join-Path $SrcDir "startup_clock.asm")
if ($LASTEXITCODE -ne 0) {
    throw "Assembly failed for startup_clock.asm"
}
$relFiles += $clockRel
}

$ihxPath = Join-Path $BuildDir "cc2_lithium.ihx"
$hexName = if ($Tests) {
    "CC2_LITHIUM_TEST_DO_NOT_FLASH.hex"
} elseif ($SafeBringup) {
    "CC2_LITHIUM_SAFE.hex"
} else {
    "CC2_LITHIUM.hex"
}
$hexPath = Join-Path $BuildDir $hexName
$linkFlags = @(
    "-mmcs51",
    "--model-large",
    "--stack-auto",
    "--out-fmt-ihx",
    "--code-loc", "0x0000",
    "--code-size", "0xF000",
    "--xram-loc", "0xF600",
    "--xram-size", "0x0600",
    "--iram-size", "0x100",
    "--idata-loc", "0x21",
    "-o", $ihxPath
)
& $sdccCommand.Source @linkFlags @relFiles
if ($LASTEXITCODE -ne 0) {
    throw "SDCC link failed"
}

& $packihxCommand.Source $ihxPath | Set-Content -Path $hexPath -Encoding ascii
if ($LASTEXITCODE -ne 0) {
    throw "packihx failed"
}

$addresses = [Collections.Generic.HashSet[int]]::new()
$upper = 0
foreach ($line in Get-Content -LiteralPath $ihxPath) {
    if (-not $line.StartsWith(":")) { continue }
    $length = [Convert]::ToInt32($line.Substring(1, 2), 16)
    $offset = [Convert]::ToInt32($line.Substring(3, 4), 16)
    $type = [Convert]::ToInt32($line.Substring(7, 2), 16)
    if ($type -eq 0) {
        for ($i = 0; $i -lt $length; $i++) {
            [void]$addresses.Add($upper + $offset + $i)
        }
    } elseif ($type -eq 2) {
        $upper = [Convert]::ToInt32($line.Substring(9, 4), 16) -shl 4
    } elseif ($type -eq 4) {
        $upper = [Convert]::ToInt32($line.Substring(9, 4), 16) -shl 16
    }
}

$pFlashLimit = 0xF000
$programmed = @($addresses | Where-Object { $_ -lt $pFlashLimit }).Count
$maximum = [int](($addresses | Measure-Object -Maximum).Maximum)
if ($maximum -ge $pFlashLimit) {
    throw ("Image reached reserved code space at 0x{0:X4}." -f $maximum)
}
$contiguousFree = $pFlashLimit - ($maximum + 1)
$hexHash = (Get-FileHash -Algorithm SHA256 $hexPath).Hash.ToLowerInvariant()
[ordered]@{
    image = $hexName
    profile = $(if ($Tests) { "simulator-only" } elseif ($SafeBringup) { "safe" } else { "external-bms-default" })
    sha256 = $hexHash
    version = $BuildId
    build_id = $BuildId
    protocol_sha256 = $protocolHash
    p_capacity = $pFlashLimit
    p_used = $programmed
    p_free = $pFlashLimit - $programmed
    contiguous_free = $contiguousFree
    last_address = $maximum
    d_used = 0
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $BuildDir "build_info.json") -Encoding ascii
Write-Host ("P-Flash: {0} programmed bytes, {1} free by count, {2} contiguous bytes after 0x{3:X4}." -f `
    $programmed, ($pFlashLimit - $programmed), $contiguousFree, $maximum)
Write-Host ("Automatic drive control: {0}" -f $(if ($SafeBringup) { "DISABLED" } else { "ENABLED" }))
Get-FileHash -Algorithm SHA256 $hexPath | Format-List
Get-Item $ihxPath, $hexPath | Format-Table Name,Length,LastWriteTime
