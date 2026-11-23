param([string]$Sdcc = "sdcc", [string]$Packihx = "packihx", [string]$Simulator = "s51", [string]$Python = "python")
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
& (Join-Path $Root "tools/verify_all.ps1") -Sdcc $Sdcc -Packihx $Packihx -Simulator $Simulator -Python $Python
$release = Join-Path $Root "releases/v0.1.0"
New-Item -ItemType Directory -Force -Path $release | Out-Null
foreach ($profile in @("build", "build_safe")) {
    $build = Join-Path $Root "firmware/$profile"
    $info = Get-Content -LiteralPath (Join-Path $build "build_info.json") -Raw | ConvertFrom-Json
    Copy-Item -LiteralPath (Join-Path $build $info.image) -Destination $release -Force
    Copy-Item -LiteralPath (Join-Path $build "build_info.json") -Destination (Join-Path $release "$profile.json") -Force
    Copy-Item -LiteralPath (Join-Path $build "integration_verification.json") -Destination (Join-Path $release "$profile-integration.json") -Force
}
Copy-Item -LiteralPath (Join-Path $Root "firmware/build_test/verification.json") -Destination (Join-Path $release "core-verification.json") -Force
$sourceFiles = @(
    Get-Item -LiteralPath (Join-Path $Root "README.md"), (Join-Path $Root ".gitignore"), (Join-Path $Root "firmware/build.ps1")
    Get-ChildItem -LiteralPath (Join-Path $Root "firmware/src"), (Join-Path $Root "firmware/include"), `
        (Join-Path $Root "tools"), (Join-Path $Root "tests"), (Join-Path $Root "docs") -File
)
$sourceHashes = [ordered]@{}
foreach ($file in $sourceFiles | Sort-Object FullName) {
    $relative = $file.FullName.Substring($Root.Length + 1).Replace('\', '/')
    $sourceHashes[$relative] = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash.ToLowerInvariant()
}
[ordered]@{
    version = "0.1.0"
    name = "PoldenTEK CC2 Lithium"
    default_charge_mode = "EXTERNAL/FORCE - independent charger/BMS required"
    hardware_validation = "Not performed for this version; bench firmware"
    source_sha256 = $sourceHashes
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $release "manifest.json") -Encoding ascii
Get-ChildItem -LiteralPath $release -Filter *.hex | ForEach-Object {
    "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant(), $_.Name
} | Set-Content -LiteralPath (Join-Path $release "SHA256SUMS.txt") -Encoding ascii
Write-Host "Verified release: $release"
