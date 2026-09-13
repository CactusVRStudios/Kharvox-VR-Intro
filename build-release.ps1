
$ErrorActionPreference = 'Stop'
function Run([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE) { throw "$Program failed ($LASTEXITCODE)" }
}
function FetchArchive([string]$Url,[string]$Path,[string]$Hash,[string]$Destination) {
    if (-not (Test-Path $Path)) { Invoke-WebRequest $Url -OutFile $Path }
    if ((Get-FileHash $Path).Hash -ne $Hash) { throw "Download hash mismatch: $Path" }
    if (-not (Test-Path $Destination)) { [IO.Compression.ZipFile]::ExtractToDirectory($Path,$Destination) }
}
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force build | Out-Null
    $revision = 'b76b80adaf65ac3ad6cc1ce61974fb29a5d02352'
    FetchArchive "https://codeload.github.com/KhronosGroup/OpenXR-SDK/zip/$revision" "$PSScriptRoot/build/openxr-sdk.zip" 'A7DB9DC8C941B3520723C4D224A0070467BAB085CD78C072A2C31AA10EEEB922' "$PSScriptRoot/build/openxr-source"
    $source = "$PSScriptRoot/build/openxr-source/OpenXR-SDK-$revision"
    Run cmake @('-S','tools/pc-loader','-B','build/pc-loader-static','-A','x64',"-DOPENXR_SOURCE_DIR=$source",'-Wno-dev')
    Run cmake @('--build','build/pc-loader-static','--config','Release','--target','openxr_loader','--parallel')
    $loader = "$PSScriptRoot/build/pc-loader-static/sdk/src/loader/Release/openxr_loader.lib"
    $assetCompression = "ON"
    Run cmake @('-S','.','-B','build','-A','x64',"-DKHARVOX_OPENXR_LOADER_LIB=$loader","-DKHARVOX_COMPRESS_ASSETS=$assetCompression")
    Run cmake @('--build','build','--config','Release','--parallel')
    Run ctest @('--test-dir','build','-C','Release','--output-on-failure')
    Run cmake @('--install','build','--config','Release','--prefix','Release')
    Copy-Item 'build/Release/KHARVOX Intro.exe' 'Release/KHARVOX Intro.exe' -Force

    Run "$PSScriptRoot/build/Release/IntroLoaderTests.exe" @()
    $files = @(Get-Item 'Release/KHARVOX Intro.exe' | ForEach-Object {
        [pscustomobject][ordered]@{ name=$_.Name; bytes=$_.Length; sha256=(Get-FileHash $_.FullName).Hash }
    })
    [ordered]@{ packed=$false; staticOpenXR=$true; files=$files; totalBytes=($files.bytes | Measure-Object -Sum).Sum } |
        ConvertTo-Json -Depth 4 | Set-Content build/PC-BUILD-INFO.json
    $files | Format-Table name,bytes
} finally { Pop-Location }
