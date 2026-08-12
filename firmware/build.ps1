param(
    [string]$Sdcc = "sdcc",
    [string]$Packihx = "packihx",
    [switch]$Clean,
    [switch]$EnableOutputTests,
    [switch]$SafeBringup
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if ($EnableOutputTests -and $SafeBringup) {
    throw "-EnableOutputTests and -SafeBringup are separate image types."
}
if ($EnableOutputTests) {
    $BuildDir = Join-Path $Root "build_output_test"
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
    if (-not $resolvedBuild.StartsWith($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean outside the firmware directory."
    }
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

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
    "-I$IncludeDir"
)
if ($EnableOutputTests) {
    $commonFlags += "-DCC2_ENABLE_OUTPUT_TESTS=1"
}
if ($SafeBringup) {
    $commonFlags += "-DCC2_ENABLE_DRIVE_CONTROL=0"
}

$relFiles = @()
foreach ($source in @(
    "hardware.c",
    "uart.c",
    "timebase.c",
    "speed.c",
    "i2c_bus.c",
    "display.c",
    "display_eeprom.c",
    "vehicle_eeprom.c",
    "drive.c",
    "commands.c",
    "main.c"
)) {
    $sourcePath = Join-Path $SrcDir $source
    $relPath = Join-Path $BuildDir ([IO.Path]::ChangeExtension($source, ".rel"))
    & $sdccCommand.Source @commonFlags -c $sourcePath -o $relPath
    if ($LASTEXITCODE -ne 0) {
        throw "SDCC compile failed for $source"
    }
    $relFiles += $relPath
}

$clockRel = Join-Path $BuildDir "startup_clock.rel"
& $assembler -los $clockRel (Join-Path $SrcDir "startup_clock.asm")
if ($LASTEXITCODE -ne 0) {
    throw "Assembly failed for startup_clock.asm"
}
$relFiles += $clockRel

$ihxPath = Join-Path $BuildDir "cc2_custom.ihx"
$hexName = if ($EnableOutputTests) {
    "CC2_BASIC_DRIVE_OUTPUT_TEST.hex"
} elseif ($SafeBringup) {
    "CC2_BASIC_DRIVE_SAFE.hex"
} else {
    "CC2_BASIC_DRIVE.hex"
}
$hexPath = Join-Path $BuildDir $hexName
$linkFlags = @(
    "-mmcs51",
    "--model-large",
    "--stack-auto",
    "--out-fmt-ihx",
    "--code-loc", "0x0000",
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
Write-Host ("P-Flash: {0} programmed bytes, {1} free by count, {2} contiguous bytes after 0x{3:X4}." -f `
    $programmed, ($pFlashLimit - $programmed), $contiguousFree, $maximum)
Write-Host ("Output test commands: {0}" -f $(if ($EnableOutputTests) { "ENABLED" } else { "LOCKED" }))
Write-Host ("Automatic drive control: {0}" -f $(if ($SafeBringup) { "DISABLED" } else { "ENABLED" }))
Get-FileHash -Algorithm SHA256 $hexPath | Format-List
Get-Item $ihxPath, $hexPath | Format-Table Name,Length,LastWriteTime
