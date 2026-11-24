param([string]$Sdcc = "sdcc", [string]$Packihx = "packihx", [string]$Simulator = "s51", [string]$Python = "python", [string]$BuildId = "", [string]$DesktopSource = "", [string]$OutputDirectory = "", [switch]$SkipDesktop)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
& (Join-Path $Root "tools/verify_all.ps1") -Sdcc $Sdcc -Packihx $Packihx -Simulator $Simulator -Python $Python -BuildId $BuildId -DesktopSource $DesktopSource -SkipDesktop:$SkipDesktop
$normalInfo = Get-Content -LiteralPath (Join-Path $Root 'firmware/build/build_info.json') -Raw | ConvertFrom-Json
$version = $normalInfo.version
if (-not $version -or $version -ne $normalInfo.build_id) { throw 'Firmware version must equal its release build ID.' }
$release = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $Root "releases/$version" }
foreach ($source in @((Join-Path $Root 'firmware/build/CC2_LITHIUM.hex'),
                       (Join-Path $Root 'firmware/build_safe/CC2_LITHIUM_SAFE.hex'),
                       (Join-Path $Root 'protocol/protocol.json'))) {
    $existing = Join-Path $release (Split-Path -Leaf $source)
    if ((Test-Path -LiteralPath $existing) -and
        (Get-FileHash -LiteralPath $existing -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash) {
        throw "Release HEX/protocol bytes are immutable; choose a new BuildId: $version"
    }
}
New-Item -ItemType Directory -Force -Path $release | Out-Null
foreach ($profile in @("build", "build_safe")) {
    $build = Join-Path $Root "firmware/$profile"
    $info = Get-Content -LiteralPath (Join-Path $build "build_info.json") -Raw | ConvertFrom-Json
    if ($info.version -ne $version -or $info.build_id -ne $version) { throw 'Build profiles disagree on their release version.' }
    Copy-Item -LiteralPath (Join-Path $build $info.image) -Destination $release -Force
    Copy-Item -LiteralPath (Join-Path $build "build_info.json") -Destination (Join-Path $release "$profile.json") -Force
    Copy-Item -LiteralPath (Join-Path $build "integration_verification.json") -Destination (Join-Path $release "$profile-integration.json") -Force
    Copy-Item -LiteralPath (Join-Path $build 'schema_verification.json') -Destination (Join-Path $release "$profile-schema.json") -Force
    Copy-Item -LiteralPath (Join-Path $build 'contract_verification.json') -Destination (Join-Path $release "$profile-contract.json") -Force
    Copy-Item -LiteralPath (Join-Path $build 'report_transcripts.json') -Destination (Join-Path $release "$profile-transcripts.json") -Force
}
Copy-Item -LiteralPath (Join-Path $Root "firmware/build_test/verification.json") -Destination (Join-Path $release "core-verification.json") -Force
Copy-Item -LiteralPath (Join-Path $Root 'protocol/protocol.json') -Destination (Join-Path $release 'protocol.json') -Force
$sourceFiles = @(
    Get-Item -Force -LiteralPath (Join-Path $Root "README.md"), (Join-Path $Root ".gitignore"), (Join-Path $Root '.gitattributes'), (Join-Path $Root "firmware/build.ps1")
    Get-ChildItem -LiteralPath (Join-Path $Root "firmware/src"), (Join-Path $Root "firmware/include"), `
        (Join-Path $Root "tools"), (Join-Path $Root "tests"), (Join-Path $Root "docs"), (Join-Path $Root 'protocol') -File
)
$sourceHashes = [ordered]@{}
foreach ($file in $sourceFiles | Sort-Object FullName) {
    $relative = $file.FullName.Substring($Root.Length + 1).Replace('\', '/')
    $sourceHashes[$relative] = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash.ToLowerInvariant()
}
[ordered]@{
    version = $version
    build_id = $info.build_id
    protocol_sha256 = $info.protocol_sha256
    protocol_url = "https://filer.poldentek.no/buddy/old-cc-firmware/lithium/releases/$($info.build_id)/protocol.json"
    name = "PoldenTEK CC2 Lithium"
    default_charge_mode = "EXTERNAL/FORCE - independent charger/BMS required"
    hardware_validation = "Not performed for this version; bench firmware"
    source_sha256 = $sourceHashes
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $release "manifest.json") -Encoding ascii
Get-ChildItem -LiteralPath $release -Filter *.hex | ForEach-Object {
    "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant(), $_.Name
} | Set-Content -LiteralPath (Join-Path $release "SHA256SUMS.txt") -Encoding ascii
Write-Host "Verified release: $release"
