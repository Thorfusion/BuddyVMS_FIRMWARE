param([string]$Sdcc = "sdcc", [string]$Packihx = "packihx", [string]$Simulator = "s51", [string]$Python = "python")
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$build = Join-Path $Root "firmware/build.ps1"
& $build -Sdcc $Sdcc -Packihx $Packihx
& $build -Sdcc $Sdcc -Packihx $Packihx -SafeBringup
& $build -Sdcc $Sdcc -Packihx $Packihx -Tests
& $Python -B (Join-Path $Root "tools/verify.py") --simulator $Simulator
if ($LASTEXITCODE) { throw "Compiled core tests failed." }
& $Python -B (Join-Path $Root "tools/verify_integration.py") --simulator $Simulator
if ($LASTEXITCODE) { throw "Operational integration tests failed." }
& $Python -B (Join-Path $Root "tools/verify_integration.py") --simulator $Simulator --safe
if ($LASTEXITCODE) { throw "Safe-image integration tests failed." }
& $Python -B -m unittest discover -s (Join-Path $Root "tests") -p test_protocol_host.py
if ($LASTEXITCODE) { throw "Host protocol tests failed." }
