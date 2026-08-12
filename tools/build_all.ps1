param(
    [string]$Sdcc = "sdcc",
    [string]$Packihx = "packihx"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Build = Join-Path $Root "firmware\build.ps1"

& $Build -Clean -Sdcc $Sdcc -Packihx $Packihx
& $Build -Clean -SafeBringup -Sdcc $Sdcc -Packihx $Packihx
& $Build -Clean -EnableOutputTests -Sdcc $Sdcc -Packihx $Packihx

Write-Host "Built normal, safe-bringup, and output-test images."

