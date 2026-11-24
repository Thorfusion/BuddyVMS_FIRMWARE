param([string]$Sdcc = "sdcc", [string]$Packihx = "packihx", [string]$Simulator = "s51", [string]$Python = "python", [string]$BuildId = "", [string]$DesktopSource = "", [switch]$SkipDesktop)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$build = Join-Path $Root "firmware/build.ps1"
& $build -Sdcc $Sdcc -Packihx $Packihx -BuildId $BuildId
$BuildId = (Get-Content -LiteralPath (Join-Path $Root 'firmware/build/build_info.json') -Raw | ConvertFrom-Json).build_id
& $build -Sdcc $Sdcc -Packihx $Packihx -SafeBringup -BuildId $BuildId
& $build -Sdcc $Sdcc -Packihx $Packihx -Tests -BuildId $BuildId
& $Python -B (Join-Path $Root "tools/verify.py") --simulator $Simulator
if ($LASTEXITCODE) { throw "Compiled core tests failed." }
& $Python -B (Join-Path $Root "tools/verify_integration.py") --simulator $Simulator
if ($LASTEXITCODE) { throw "Operational integration tests failed." }
& $Python -B (Join-Path $Root "tools/verify_integration.py") --simulator $Simulator --safe
if ($LASTEXITCODE) { throw "Safe-image integration tests failed." }
$desktopArgs = @()
if ($DesktopSource) { $desktopArgs = @('--desktop-source', $DesktopSource) }
foreach ($profile in @('build', 'build_safe')) {
    $profileArgs = @()
    if ($profile -eq 'build_safe') { $profileArgs = @('--safe') }
    & $Python -B (Join-Path $Root 'tools/verify_contract.py') @profileArgs
    if ($LASTEXITCODE) { throw 'Offline schema contract failed.' }
}
if (-not $SkipDesktop) {
& $Python -B (Join-Path $Root 'tools/verify_reports.py') @desktopArgs
if ($LASTEXITCODE) { throw 'Desktop schema integration failed.' }
& $Python -B (Join-Path $Root 'tools/verify_reports.py') @desktopArgs --safe
if ($LASTEXITCODE) { throw 'Safe-image desktop schema integration failed.' }
} else {
    foreach ($profile in @('build', 'build_safe')) {
        @{ status = 'skipped'; scope = 'Desktop checkout not supplied; offline contract tested separately' } |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Root "firmware/$profile/schema_verification.json") -Encoding ascii
    }
    Write-Warning 'Desktop integration explicitly skipped; no GUI/desktop validation claimed.'
}
& $Python -B -m unittest discover -s (Join-Path $Root "tests") -p 'test_*host.py'
if ($LASTEXITCODE) { throw "Host protocol tests failed." }
